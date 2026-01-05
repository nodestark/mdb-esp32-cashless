module.exports = {
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
