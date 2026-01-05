<template>
  <v-container>
    <v-row justify="center" class="text-center">
      <v-col cols="12" sm="6">
        <v-card>
          <v-img
            :src="require('../assets/logo.png')"
            contain
            height="200"
          ></v-img>

          <v-card-title>
            <div class="text-center w-full">
              <div class="text--secondary">IDF version: {{version}}</div>
              <div class="text--secondary">ESP cores: {{cores}}</div>
              <div class="text--secondary">WiFi SSID: {{wifi_ssid}}</div>
              <div class="text--secondary">WiFi Password: {{wifi_password}}</div>
            </div>
          </v-card-title>
        </v-card>
      </v-col>
    </v-row>
  </v-container>
</template>

<script>
export default {
  data () {
    return {
      version: null,
      cores: null,
      wifi_ssid: null,
      wifi_password: null
    }
  },
  mounted () {
    // Assuming $ajax is an axios instance attached to Vue prototype
    this.$ajax
      .get('/api/v1/system/info')
      .then(response => {
        // Axios wraps data in a 'data' property
        const data = response.data
        this.version = data.version
        this.cores = data.cores
        this.wifi_ssid = data.wifi_ssid
        this.wifi_password = data.wifi_password
      })
      .catch(error => {
        console.error(error)
      })
  }
}
</script>
