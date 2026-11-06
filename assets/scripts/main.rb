# Runs at startup.
#
# On Android this copy is bundled in the APK. To try changes without
# rebuilding, push a replacement and restart the app:
#   make push-script     (copies assets/scripts/main.rb to the device)

puts "main.rb: mruby #{MRUBY_VERSION}"

# The printer's Moonraker server.
moonraker "192.168.1.100", 7125
