import Aedes from "aedes";
import net, { type Server } from "node:net";

export interface MqttBrokerHandle {
  broker: Aedes;
  server: Server;
  close: () => Promise<void>;
}

export async function startMqttBroker(port: number, host: string): Promise<MqttBrokerHandle> {
  const broker = new Aedes();
  const server = net.createServer(broker.handle);

  await new Promise<void>((resolve, reject) => {
    const onError = (error: Error) => {
      server.off("listening", onListening);
      reject(error);
    };
    const onListening = () => {
      server.off("error", onError);
      resolve();
    };

    server.once("error", onError);
    server.once("listening", onListening);
    server.listen(port, host);
  });

  return {
    broker,
    server,
    close: async () => {
      await new Promise<void>((resolve) => server.close(() => resolve()));
      await new Promise<void>((resolve) => broker.close(() => resolve()));
    }
  };
}
