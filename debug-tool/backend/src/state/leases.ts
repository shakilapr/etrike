export type LeaseResource = "steer" | "motor" | "brake" | "sys";

export interface LeaseInfo {
  resource: LeaseResource;
  ownerId: string;
  expiresAt: number;
}

export class LeaseManager {
  private leases = new Map<LeaseResource, LeaseInfo>();
  private readonly defaultTtlMs = 10000;

  acquire(resource: LeaseResource, ownerId: string, ttlMs: number = this.defaultTtlMs): boolean {
    const current = this.leases.get(resource);
    const now = Date.now();

    if (current && current.ownerId !== ownerId && current.expiresAt > now) {
      // Held by someone else and not expired
      return false;
    }

    this.leases.set(resource, {
      resource,
      ownerId,
      expiresAt: now + ttlMs
    });
    return true;
  }

  renew(resource: LeaseResource, ownerId: string, ttlMs: number = this.defaultTtlMs): boolean {
    const current = this.leases.get(resource);
    const now = Date.now();

    if (!current || current.ownerId !== ownerId || current.expiresAt <= now) {
      // Cannot renew a lease you don't hold or that has already expired
      return false;
    }

    current.expiresAt = now + ttlMs;
    return true;
  }

  release(resource: LeaseResource, ownerId: string): void {
    const current = this.leases.get(resource);
    if (current && current.ownerId === ownerId) {
      this.leases.delete(resource);
    }
  }

  releaseAll(ownerId: string): void {
    for (const [resource, info] of this.leases.entries()) {
      if (info.ownerId === ownerId) {
        this.leases.delete(resource);
      }
    }
  }

  getOwner(resource: LeaseResource): string | null {
    const current = this.leases.get(resource);
    if (current && current.expiresAt > Date.now()) {
      return current.ownerId;
    }
    return null;
  }

  checkAccess(resource: LeaseResource, ownerId: string): boolean {
    const current = this.leases.get(resource);
    return !!current && current.ownerId === ownerId && current.expiresAt > Date.now();
  }

  list(): LeaseInfo[] {
    const now = Date.now();
    return Array.from(this.leases.values()).filter(l => l.expiresAt > now);
  }
}
