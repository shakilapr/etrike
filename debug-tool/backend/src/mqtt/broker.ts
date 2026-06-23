import aedesFactory from "aedes";
import { createServer, type Server } from "node:net";

export interface MqttBrokerHandle {
  close(): Promise<void>;
}

// aedes type definitions declare a class, but the runtime export is a factory function.
// Cast to callable to work around the type mismatch.
const createAedes = aedesFactory as unknown as () => InstanceType<typeof aedesFactory>;

export async function startMqttBroker(port: number, host: string): Promise<MqttBrokerHandle> {
  const aedes = createAedes();

  return new Promise<MqttBrokerHandle>((resolve, reject) => {
    const server: Server = createServer(aedes.handle as (stream: unknown) => void);

    server.once("error", reject);

    server.listen(port, host, () => {
      resolve({
        async close() {
          return new Promise<void>((resolveClose) => {
            aedes.close(() => {
              server.close(() => resolveClose());
            });
          });
        }
      });
    });
  });
}
