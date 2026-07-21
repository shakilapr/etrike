import { useEffect, useState } from 'react'
import { Input } from './ui/input'

export function NumericDraft({
  value,
  onValue,
  min,
  max,
  testId,
  disabled,
}: {
  value: number
  onValue: (value: number) => void
  min?: number
  max?: number
  testId?: string
  disabled?: boolean
}) {
  const [draft, setDraft] = useState(String(value))
  const [editing, setEditing] = useState(false)
  const parsed = draft.trim() === '' || draft.trim() === '-' ? NaN : Number(draft)
  const valid =
    Number.isFinite(parsed) &&
    (min == null || parsed >= min) &&
    (max == null || parsed <= max)

  useEffect(() => {
    if (!editing) setDraft(String(value))
  }, [editing, value])

  return (
    <Input
      type="text"
      inputMode="decimal"
      data-testid={testId}
      value={draft}
      disabled={disabled}
      aria-invalid={!valid}
      className="w-full max-w-[8rem] font-mono text-[12.5px]"
      onFocus={() => setEditing(true)}
      onChange={(event) => {
        const next = event.target.value
        setDraft(next)
        const number = Number(next)
        if (
          next.trim() !== '' &&
          next.trim() !== '-' &&
          Number.isFinite(number) &&
          (min == null || number >= min) &&
          (max == null || number <= max)
        ) {
          onValue(number)
        }
      }}
      onBlur={() => {
        setEditing(false)
        if (!valid) setDraft(String(value))
      }}
    />
  )
}
