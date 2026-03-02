import { useDark, useToggle } from '@vueuse/core'

export function useTheme() {
  const isDark = useDark({
    selector: 'html',
    attribute: 'class',
    valueDark: 'dark',
    valueLight: '',
    storageKey: 'color-scheme',
  })

  const toggleTheme = useToggle(isDark)

  return { isDark, toggleTheme }
}
