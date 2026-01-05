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
            <div class="text-left w-full">
              <div class="text--secondary">IDF version: {{systemInfo.version}}</div>
              <div class="text--secondary">ESP cores: {{systemInfo.cores}}</div>
              <div class="text--secondary">WiFi SSID: {{systemInfo.wifi_ssid}}</div>
              <div class="text--secondary">WiFi Password: {{systemInfo.wifi_password}}</div>
              <div class="text--secondary">MQTT Server: {{systemInfo.mqtt_server}}</div>
            </div>
          </v-card-title>
        </v-card>
      </v-col>
      <v-col cols="12" sm="6">
        <v-card>
          <v-card-title>
           Settings
          </v-card-title>
          <v-card-text>
            <v-text-field v-model="wifiForm.ssid" label="WiFi SSID"></v-text-field>
            <v-text-field v-model="wifiForm.password" label="WiFi Password"></v-text-field>
            <v-text-field v-model="mqttForm.server" label="MQTT Server Address"></v-text-field>
          </v-card-text>
          <v-card-actions>
            <v-btn @click="saveSettingData()">Save</v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>
  </v-container>
</template>

<script>
export default {
  data () {
    return {
      mqttForm: {
        server: ''
      },
      wifiForm: {
        ssid: '',
        password: ''
      },
      systemInfo: {
        version: null,
        cores: null,
        wifi_ssid: null,
        wifi_password: null
      }
    }
  },
  methods: {
    saveSettingData: function () {
      console.log('clicked settings save button')
      console.log(`SSID: ${this.wifiForm.ssid}`)
      console.log(`Password: ${this.wifiForm.password}`)
      console.log(`MQTT Server: ${this.mqttForm.password}`)
      const settings = {
        'url': '/api/v1/settings/set',
        'method': 'POST',
        'timeout': 0,
        'headers': {
          'Content-Type': 'application/json'
        },
        'data': JSON.stringify({
          'ssid': this.wifiForm.ssid,
          'password': this.wifiForm.password,
          'mqtt_server': this.mqttForm.server
        })
      }

      this.$ajax(settings).done(function (response) {
        console.log(response)
      })
    }
  },
  mounted () {
    // Assuming $ajax is an axios instance attached to Vue prototype
    this.$ajax
      .get('/api/v1/system/info')
      .then(response => {
        // Axios wraps data in a 'data' property
        const data = response.data
        this.systemInfo.version = data.version
        this.systemInfo.cores = data.cores
        this.systemInfo.wifi_ssid = data.wifi_ssid
        this.systemInfo.wifi_password = data.wifi_password
        this.systemInfo.mqtt_server = data.mqtt_server
      })
      .catch(error => {
        console.error(error)
      })
  }
}

</script>
