import { Modal, ModalContent, ModalHeader, ModalBody, ModalFooter, Button } from '@heroui/react'
import React, { useEffect, useState } from 'react'
import { BaseEditor } from '../base/base-editor-lazy'
import { getFileStr, setFileStr } from '@renderer/utils/ipc'
import yaml from 'js-yaml'
import { useAppConfig } from '@renderer/hooks/use-app-config'
type Language = 'yaml' | 'javascript' | 'css' | 'json' | 'text'

interface Props {
  onClose: () => void
  path: string
  type: string
  title: string
  privderType: string
  format?: string
}
const Viewer: React.FC<Props> = (props) => {
  const { type, path, title, format, privderType, onClose } = props
  const { appConfig: { disableAnimation = false } = {} } = useAppConfig()
  const [currData, setCurrData] = useState('')
  const [saving, setSaving] = useState(false)
  let language: Language = !format || format === 'YamlRule' ? 'yaml' : 'text'

  const getContent = async (): Promise<void> => {
    try {
      let fileContent: string
      console.log('Viewer: Loading content', { type, path, title, privderType })
      if (type === 'Inline') {
        fileContent = await getFileStr('config.yaml')
        language = 'yaml'
      } else if (path) {
        fileContent = await getFileStr(path)
      } else {
        console.error('Viewer: Path is empty for non-Inline type')
        setCurrData('')
        return
      }
      try {
        const parsedYaml = yaml.load(fileContent)
        if (parsedYaml && typeof parsedYaml === 'object') {
          const yamlObj = parsedYaml as Record<string, unknown>
          const payload = yamlObj[privderType]?.[title]?.payload
          if (payload) {
            if (privderType === 'proxy-providers') {
              setCurrData(
                yaml.dump({
                  proxies: payload
                })
              )
            } else {
              setCurrData(
                yaml.dump({
                  rules: payload
                })
              )
            }
          } else {
            const targetObj = yamlObj[privderType]?.[title]
            if (targetObj) {
              setCurrData(yaml.dump(targetObj))
            } else {
              console.log('Viewer: Using raw file content (target object not found)')
              setCurrData(fileContent)
            }
          }
        } else {
          console.log('Viewer: Using raw file content (not an object or empty)')
          setCurrData(fileContent)
        }
      } catch (error) {
        console.error('Viewer: Failed to parse YAML, using raw content:', error)
        setCurrData(fileContent)
      }
    } catch (error) {
      console.error('Viewer: Failed to load file content:', error)
      alert(`加载文件失败: ${error instanceof Error ? error.message : String(error)}`)
    }
  }

  useEffect(() => {
    getContent()
  }, [])

  return (
    <Modal
      backdrop={disableAnimation ? 'transparent' : 'blur'}
      disableAnimation={disableAnimation}
      classNames={{
        base: 'max-w-none w-full',
        backdrop: 'top-[48px]'
      }}
      size="5xl"
      hideCloseButton
      isOpen={true}
      onOpenChange={onClose}
      scrollBehavior="inside"
    >
      <ModalContent className="h-full w-[calc(100%-100px)]">
        <ModalHeader className="flex pb-0 app-drag">{title}</ModalHeader>
        <ModalBody className="h-full">
          <BaseEditor
            language={language}
            value={currData}
            readOnly={type != 'File'}
            onChange={(value) => setCurrData(value)}
          />
        </ModalBody>
        <ModalFooter className="pt-0">
          <Button size="sm" variant="light" onPress={onClose}>
            关闭
          </Button>
          {type == 'File' && (
            <Button
              size="sm"
              color="primary"
              isLoading={saving}
              onPress={async () => {
                setSaving(true)
                try {
                  await setFileStr(path, currData)
                  onClose()
                } catch (error) {
                  console.error('Save failed:', error)
                  alert(`保存失败: ${error instanceof Error ? error.message : String(error)}`)
                } finally {
                  setSaving(false)
                }
              }}
            >
              保存
            </Button>
          )}
        </ModalFooter>
      </ModalContent>
    </Modal>
  )
}

export default Viewer
