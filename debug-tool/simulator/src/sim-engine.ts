import mqtt from "mqtt";
import { DEFAULT_PROFILE, generateFrame, generateStats, tickSimTime, type Profile } from "./can-generator";

export class SimEngine {
  private client: mqtt.MqttClient | null = null;
  private timers: Map<string, ReturnType<typeof setInterval>> = new Map();
  private busStats = new Map<string, { count: number; active: boolean }>();
  private started = false;
  private readonly topicPrefix: string;

  constructor(
    private readonly brokerUrl: string,
    private readonly profile: Profile = DEFAULT_PROFILE,
    topicPrefix = "etrike/debug"
  ) {
    this.topicPrefix = topicPrefix;
    // Initialize bus stats
    for (const entry of profile) {
      if (!this.busStats.has(entry.bus)) {
        this.busStats.set(entry.bus, { count: 0, active: true });
      }
    }
  }

  start(): void {
    if (this.started) return;
    this.started = true;

    this.client = mqtt.connect(this.brokerUrl, {
      clientId: `etrike-sim-${Date.now()}`,
      clean: true,
      reconnectPeriod: 2000,
    });

    this.client.on("connect", () => {
      console.log(`[sim] Connected to MQTT broker at ${this.brokerUrl}`);

      // Subscribe to command topics for injection feedback
      this.client?.subscribe("etrike/debug/cmd/send");
      this.client?.subscribe("etrike/debug/cmd/send/periodic");

      // Start per-message timers
      for (const entry of this.profile) {
        this.startEntryTimer(entry);
      }

      // Stats heartbeat (1 Hz)
      this.timers.set("__stats__", setInterval(() => this.publishStats(), 1000));

      // Status heartbeat (5 s)
      this.timers.set("__status__", setInterval(() => this.publishStatus(), 5000));
    });

    this.client.on("message", (topic, payload) => {
      try {
        const msg = JSON.parse(payload.toString());
        // Echo back a cmd_ack for received commands
        const responseTopic = this.topicPrefix + "/cmd/response";
        this.client?.publish(responseTopic, JSON.stringify({
          type: "cmd_ack",
          status: "received",
          correlation_id: msg.correlation_id ?? null,
          ts: Date.now() / 1000,
        }));
      } catch {
        // ignore malformed commands
      }
    });

    this.client.on("error", (err) => {
      console.error("[sim] MQTT error:", err.message);
    });

    this.client.on("close", () => {
      console.log("[sim] MQTT disconnected");
    });
  }

  stop(): void {
    this.started = false;
    for (const timer of this.timers.values()) {
      clearInterval(timer);
    }
    this.timers.clear();
    if (this.client) {
      this.client.end(true);
      this.client = null;
    }
  }

  private startEntryTimer(entry: Profile[number]): void {
    const key = `${entry.bus}:${entry.id}`;
    this.timers.set(key, setInterval(() => {
      tickSimTime(entry.interval_ms);
      const frame = generateFrame(entry);

      // Update stats
      const stats = this.busStats.get(entry.bus);
      if (stats) stats.count++;

      // Publish to MQTT
      const topic = `${this.topicPrefix}/can/rx/${entry.bus}`;
      this.client?.publish(topic, JSON.stringify(frame));
    }, entry.interval_ms));
  }

  private publishStats(): void {
    const stats = generateStats(this.busStats);
    this.client?.publish(`${this.topicPrefix}/can/stats`, JSON.stringify(stats));
  }

  private publishStatus(): void {
    this.client?.publish(`${this.topicPrefix}/status`, JSON.stringify({
      type: "status",
      adapter_connected: true,
      online: true,
      uptime_s: Math.floor(process.uptime()),
    }));
  }
}
