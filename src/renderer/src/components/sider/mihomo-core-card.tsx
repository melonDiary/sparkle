import { Button, Card, CardBody, CardFooter, Tooltip } from '@heroui/react'
import { calcTraffic } from '@renderer/utils/calc'
import { mihomoVersion, restartCore } from '@renderer/utils/ipc'
import React, { useEffect, useState } from 'react'
import { IoMdRefresh } from 'react-icons/io'
import { useSortable } from '@dnd-kit/sortable'
import { CSS } from '@dnd-kit/utilities'
import { useLocation, useNavigate } from 'react-router-dom'
import PubSub from 'pubsub-js'
import useSWR from 'swr'
import { IPC_EVENTS } from '../../../../shared/ipc'
import { useAppConfig } from '@renderer/hooks/use-app-config'
import { LuCpu } from 'react-icons/lu'
import { notify } from '@renderer/utils/notification'

interface Props {
  iconOnly?: boolean
}

const MihomoCoreCard: React.FC<Props> = (props) => {
  const { appConfig } = useAppConfig()
  const { iconOnly } = props
  const { mihomoCoreCardStatus = 'col-span-2', disableAnimation = false } = appConfig || {}
  const { data: version, error: versionError, mutate } = useSWR('mihomoVersion', mihomoVersion, {
    errorRetryInterval: 1000,
    errorRetryCount: 5
  })
  const location = useLocation()
  const navigate = useNavigate()
  const match = location.pathname.includes('/mihomo')
  const {
    attributes,
    listeners,
    setNodeRef,
    transform: tf,
    transition,
    isDragging
  } = useSortable({
    id: 'mihomo'
  })
  const transform = tf ? { x: tf.x, y: tf.y, scaleX: 1, scaleY: 1 } : null
  const [mem, setMem] = useState(0)
  const [restarting, setRestarting] = useState(false)

  useEffect(() => {
    const token = PubSub.subscribe('mihomo-core-changed', () => {
      mutate()
    })
    const unsubscribeMihomoMemory = window.electron.ipcRenderer.on(
      IPC_EVENTS.MIHOMO_MEMORY,
      (_e, info: ControllerMemory) => {
        setMem(info.inuse)
      }
    )
    const unsubscribeCoreStarted = window.electron.ipcRenderer.on(IPC_EVENTS.CORE_STARTED, () => {
      mutate()
    })
    return (): void => {
      PubSub.unsubscribe(token)
      unsubscribeMihomoMemory()
      unsubscribeCoreStarted()
    }
  }, [])

  if (iconOnly) {
    return (
      <div className={`${mihomoCoreCardStatus} flex justify-center`}>
        <Tooltip content="内核设置" placement="right">
          <Button
            size="sm"
            isIconOnly
            color={match ? 'primary' : 'default'}
            variant={match ? 'solid' : 'light'}
            onPress={() => {
              navigate('/mihomo')
            }}
          >
            <LuCpu className="text-[20px]" />
          </Button>
        </Tooltip>
      </div>
    )
  }

  return (
    <div
      style={{
        position: 'relative',
        transform: CSS.Transform.toString(transform),
        transition,
        zIndex: isDragging ? 'calc(infinity)' : undefined
      }}
      className={`${mihomoCoreCardStatus} mihomo-core-card`}
    >
      {mihomoCoreCardStatus === 'col-span-2' ? (
        <Card
          fullWidth
          ref={setNodeRef}
          {...attributes}
          {...listeners}
          className={`${match ? 'bg-primary' : 'hover:bg-primary/30'} ${isDragging ? `${disableAnimation ? '' : 'scale-[0.95]'} tap-highlight-transparent` : ''}`}
        >
          <CardBody>
            <div
              ref={setNodeRef}
              {...attributes}
              {...listeners}
              className="flex justify-between h-8"
            >
              <h3
                className={`text-md font-bold leading-8 ${match ? 'text-primary-foreground' : 'text-foreground'} `}
              >
                {version?.version ?? (restarting ? '重启中' : versionError ? '未连接' : '连接中')}
              </h3>

              <Button
                isIconOnly
                size="sm"
                variant="light"
                disabled={restarting}
                color="default"
                onPress={async () => {
                  try {
                    setRestarting(true)
                    await restartCore()
                    await new Promise((resolve) => {
                      setTimeout(resolve, 2000)
                    })
                    setRestarting(false)
                  } catch (e) {
                    notify(e, { variant: 'danger' })
                  } finally {
                    mutate()
                  }
                }}
              >
                <IoMdRefresh
                  className={`text-[24px] ${match ? 'text-primary-foreground' : 'text-foreground'} ${restarting ? 'animate-spin' : ''}`}
                />
              </Button>
            </div>
          </CardBody>
          <CardFooter className="pt-1">
            <div
              className={`flex justify-between w-full text-md font-bold ${match ? 'text-primary-foreground' : 'text-foreground'}`}
            >
              <h4>内核设置</h4>
              <h4>{versionError ? '内核未连接' : calcTraffic(mem)}</h4>
            </div>
          </CardFooter>
        </Card>
      ) : (
        <Card
          fullWidth
          ref={setNodeRef}
          {...attributes}
          {...listeners}
          className={`${match ? 'bg-primary' : 'hover:bg-primary/30'} ${isDragging ? `${disableAnimation ? '' : 'scale-[0.95]'} tap-highlight-transparent` : ''}`}
        >
          <CardBody className="pb-1 pt-0 px-0 overflow-y-visible">
            <div className="flex justify-between">
              <Button
                isIconOnly
                className="bg-transparent pointer-events-none"
                variant="flat"
                color="default"
              >
                <LuCpu
                  color="default"
                  className={`${match ? 'text-primary-foreground' : 'text-foreground'} text-[24px] font-bold`}
                />
              </Button>
            </div>
          </CardBody>
          <CardFooter className="pt-1">
            <h3
              className={`text-md font-bold ${match ? 'text-primary-foreground' : 'text-foreground'}`}
            >
              内核设置
            </h3>
          </CardFooter>
        </Card>
      )}
    </div>
  )
}

export default MihomoCoreCard
