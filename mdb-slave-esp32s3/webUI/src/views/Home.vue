<template>
  <v-container>
    <v-layout text-xs-center wrap>
      <v-flex xs12 sm6 offset-sm3>
        <v-card>
          <v-img :src="require('../assets/logo.png')" contain height="200"></v-img>
          <v-card-title primary-title>
            <div class="ma-auto">
              <span class="grey--text">IDF version: {{version}}</span>
              <br>
              <span class="grey--text">ESP cores: {{cores}}</span>
              <br>
              <span class="grey--text">WiFi SSID: {{wifi_ssid}}</span>
              <br>
              <span class="grey--text">WiFi Password: {{wifi_password}}</span>
            </div>
          </v-card-title>
        </v-card>
      </v-flex>
    </v-layout>
  </v-container>
</template>

<script>
export default {
  data() {
    return {
      version: null,
      cores: null,
      wifi_ssid: null,
      wifi_password: null
    };
  },
  mounted() {
    this.$ajax
      .get("/api/v1/system/info")
      .then(data => {
        this.version = data.data.version;
        this.cores = data.data.cores;
        this.wifi_ssid = data.data.wifi_ssid;
        this.wifi_password = data.data.wifi_password;
      })
      .catch(error => {
        console.log(error);
      });
  }
};
</script>
