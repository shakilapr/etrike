// src/api/inject.ts

export interface InjectRequest {
  message_key: string;
  bus: 'high' | 'low';
  values: Record<string, any>;
}

export async function injectCommand(request: InjectRequest): Promise<boolean> {
  try {
    const response = await fetch('http://localhost:8000/api/inject', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(request),
    });

    if (!response.ok) {
      console.error(`Inject failed: ${response.statusText}`);
      return false;
    }

    return true;
  } catch (err) {
    console.error("Network error during inject:", err);
    return false;
  }
}

export async function sendHostDriveCmd(speed_mmps: number, yaw_rate_mrad_s: number, gear: number) {
  return injectCommand({
    message_key: "host:host_drive_cmd",
    bus: "high", // Assuming HOST_DRIVE_CMD goes on high bus
    values: {
      speed_mmps,
      yaw_rate_mrad_s,
      gear
    }
  });
}
