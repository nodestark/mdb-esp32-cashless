export default defineNitroPlugin((nitroApp) => {
  nitroApp.hooks.hook('render:html', (html) => {
    html.head.unshift(
      `<script>(function(){try{var s=localStorage.getItem('color-scheme');if(s==='dark'||(s===null&&window.matchMedia('(prefers-color-scheme: dark)').matches))document.documentElement.classList.add('dark')}catch(e){}})()</script>`
    )
  })
})
