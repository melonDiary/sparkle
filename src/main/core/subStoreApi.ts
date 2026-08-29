import { requestSubStore } from './subStoreClient'

export function subStoreSubs(): Promise<SubStoreSub[]> {
  return requestSubStore<SubStoreSub[]>('api/subs')
}

export function subStoreCollections(): Promise<SubStoreSub[]> {
  return requestSubStore<SubStoreSub[]>('api/collections')
}
