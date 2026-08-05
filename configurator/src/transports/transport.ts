/**
 * DeviceTransport — the only interface the app core depends on.
 * Spec §5.1. The rest of the application must not know which
 * transport is active (mock | webhid | native).
 */
export interface DeviceTransport {
  connect(): Promise<void>;
  disconnect(): Promise<void>;
  /** Send a full 32-byte report (report ID + payload), await the reply. */
  transact(report: Uint8Array): Promise<Uint8Array>;
  isConnected(): boolean;
}
