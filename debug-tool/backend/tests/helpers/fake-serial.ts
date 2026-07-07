import { EventEmitter } from "node:events";

export class MockParser extends EventEmitter {
  // ReadlineParser mock
}

export class MockSerialPort extends EventEmitter {
  isOpen = false;
  writable = true;

  constructor(options: any) {
    super();
    // Simulate autoOpen = false behavior
  }

  pipe(dest: any) {
    // When port.pipe(parser) is called, we link them so we can emit data on parser
    this.on("data", (data) => dest.emit("data", data));
    return dest;
  }

  open(cb?: (err?: Error) => void) {
    this.isOpen = true;
    this.emit("open");
    if (cb) cb();
  }

  close(cb?: (err?: Error) => void) {
    this.isOpen = false;
    this.emit("close");
    if (cb) cb();
  }

  write(data: string) {
    this.emit("write", data);
  }
}
