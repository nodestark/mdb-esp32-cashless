<template>
  <v-app id="inspire">
    <v-navigation-drawer v-model="drawer" app clipped>
      <v-list dense nav>
        <v-list-item to="/" link>
          <v-list-item-icon>
            <v-icon>{{ icons.home }}</v-icon>
          </v-list-item-icon>
          <v-list-item-content>
            <v-list-item-title>Home</v-list-item-title>
          </v-list-item-content>
        </v-list-item>
      </v-list>
    </v-navigation-drawer>

    <v-app-bar color="red accent-4" dark app clipped-left>
      <v-app-bar-nav-icon @click.stop="drawer = !drawer"></v-app-bar-nav-icon>
      <v-toolbar-title>VMFlow</v-toolbar-title>
    </v-app-bar>

    <v-main>
      <v-container fluid fill-height>
        <router-view></router-view>
      </v-container>
    </v-main>

    <v-footer color="red accent-4" app>
      <span class="white--text">&copy; VMFlow. All rights reserved.</span>
    </v-footer>
  </v-app>
</template>

<script>
import { mdiHome } from '@mdi/js'

export default {
  name: 'App',
  data () {
    return {
      drawer: null,
      icons: {
        home: mdiHome
      }
    }
  }
}
</script>
