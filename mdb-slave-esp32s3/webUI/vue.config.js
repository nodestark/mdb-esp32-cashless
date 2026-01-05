const VuetifyLoaderPlugin = require('vuetify-loader/lib/plugin')

module.exports = {
  configureWebpack: {
    plugins: [
      new VuetifyLoaderPlugin()
    ]
  },
  transpileDependencies: ['vuetify'],
  productionSourceMap: false,
  devServer: {
    proxy: {
      '/api': {
        target: 'http://10.0.1.:80',
        changeOrigin: true,
        ws: true
      }
    }
  }
}
