// Runs only on the client, before hydration.
// Reads the saved color-scheme from localStorage and applies the .dark class
// immediately so there's no flash of the wrong theme on page load.
export default defineNuxtPlugin(() => {
  const saved = localStorage.getItem('color-scheme')
  const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches
  if (saved === 'dark' || (saved === null && prefersDark)) {
    document.documentElement.classList.add('dark')
  }
})
