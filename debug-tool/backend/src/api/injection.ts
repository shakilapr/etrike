import { ID_HOST_DRIVE_CMD, ID_RT_DRIVE_CMD, ID_RT_BRAKE_CMD, ID_HOST_LIGHT_CMD, ID_SYS_MODE_CMD, ID_SAFETY_ESTOP, type CanFrame } from "../types/can";
import type { AppContext } from "../app-context";
import type { LeaseResource } from "../state/leases";

export interface InjectionOptions {
  ownerId?: string;
  confirmEstop?: boolean;
}

export class InjectionService {
  constructor(private ctx: AppContext) {}

  public validate(frame: CanFrame, options: InjectionOptions): { allowed: boolean, error?: string } {
    const resource = this.getRequiredResource(frame.frame.id);
    if (resource) {
      if (!options.ownerId) {
        return { allowed: false, error: `Injecting ${frame.frame.id} requires ${resource} lease, but no owner_id provided` };
      }
      if (!this.ctx.leaseManager.checkAccess(resource, options.ownerId)) {
        return { allowed: false, error: `Injecting ${frame.frame.id} requires ${resource} lease. You don't hold it.` };
      }
    }

    if (this.requiresArming(frame.frame.id) && this.ctx.stateMachine.state.arm !== "armed") {
      return { allowed: false, error: `Injecting ${frame.frame.id} requires physical arming state to be 'armed'` };
    }

    if (frame.frame.id === ID_SAFETY_ESTOP && !options.confirmEstop) {
      return { allowed: false, error: "ESTOP injection requires confirm_estop=true" };
    }

    return { allowed: true };
  }

  private getRequiredResource(id: string): LeaseResource | null {
    if (id === ID_HOST_DRIVE_CMD || id === ID_RT_DRIVE_CMD) return "motor";
    if (id === ID_RT_BRAKE_CMD) return "brake";
    if (id === ID_HOST_LIGHT_CMD || id === ID_SYS_MODE_CMD) return "sys";
    return null;
  }

  private requiresArming(id: string): boolean {
    const resource = this.getRequiredResource(id);
    return resource === "motor" || resource === "brake" || resource === "steer";
  }
}
