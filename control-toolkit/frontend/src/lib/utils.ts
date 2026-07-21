import { clsx, type ClassValue } from 'clsx'
import { twMerge } from 'tailwind-merge'

/** Merge Tailwind classes (shadcn-compatible). Later phases can add more shadcn primitives on top. */
export function cn(...inputs: ClassValue[]) {
  return twMerge(clsx(inputs))
}
