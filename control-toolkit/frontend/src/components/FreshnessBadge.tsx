/** Freshness pill — uses App.css `.fresh` / `.fresh-*` semantic colors. */
export function FreshnessBadge({ value }: { value: string }) {
  return (
    <span className={`fresh fresh-${value.toLowerCase()}`} data-testid="freshness">
      {value}
    </span>
  )
}
