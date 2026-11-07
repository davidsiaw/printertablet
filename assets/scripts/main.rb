# Runs at startup, and builds the screen.
#
# On Android these scripts are bundled in the APK. To try changes without
# rebuilding, push them and restart the app:
#   make push-script     (copies assets/scripts/ to the device)

puts "main.rb: mruby #{MRUBY_VERSION}"

load_script "lib/ui.rb"
load_script "lib/worker.rb"
load_script "printer.rb"
load_script "status_bar.rb"
load_script "bed_map.rb"
load_script "confirm_slider.rb"
load_script "toolhead_panel.rb"

# The printer's Moonraker server.
PRINTER_HOST = "192.168.4.22"
PRINTER_PORT = 7125

printer = Printer.new(PRINTER_HOST, PRINTER_PORT)

screen = Group.new
screen.add StatusBar.new(printer)

# The toolhead panel on the left, filling the height above the status bar.
margin = Screen.height / 30
screen.add ToolheadPanel.new(printer, x: margin, y: margin, gap: margin / 2,
                             height: Screen.height - StatusBar.height - 2 * margin)

Screen.show screen
