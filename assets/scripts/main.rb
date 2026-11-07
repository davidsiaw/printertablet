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
load_script "button.rb"
load_script "confirm_slider.rb"
load_script "keypad_modal.rb"
load_script "toolhead_panel.rb"
load_script "macro_column.rb"
load_script "klipper_names.rb"
load_script "temperature_column.rb"
load_script "fan_list.rb"
load_script "extrude_panel.rb"
load_script "printing_modal.rb"

# The printer's Moonraker server.
PRINTER_HOST = "192.168.4.22"
PRINTER_PORT = 7125

# Buttons in the column on the right: [label, G-code], top to bottom.
MACROS = [
  ["Park", "PARK"],
]

printer = Printer.new(PRINTER_HOST, PRINTER_PORT)

screen = Group.new
screen.add StatusBar.new(printer)

# The toolhead panel on the left, filling the height above the status bar.
margin = Screen.height / 30
panel_height = Screen.height - StatusBar.height - 2 * margin
panel = screen.add ToolheadPanel.new(printer, x: margin, y: margin, gap: margin / 2, height: panel_height)

# The macro buttons on the right, as tall as the panel's X/Y/Z boxes.
column_w = Screen.width / 8
column_x = Screen.width - margin - column_w
screen.add MacroColumn.new(printer, MACROS, x: column_x, y: margin,
                           w: column_w, button_h: panel_height / 9, gap: margin / 2)

# Between the bed map and the macro buttons, two columns: temperatures, and
# fans (top half) over extrude/retract (bottom half).
gap = margin / 2
row_h = panel_height / 9   # as tall as the X/Y/Z boxes
middle_x = margin + panel.width + margin
middle_w = column_x - margin - middle_x
temps_w = (middle_w - margin) / 2
screen.add TemperatureColumn.new(printer, x: middle_x, y: margin, w: temps_w, main_h: row_h, gap: gap)

right_x = middle_x + temps_w + margin
right_w = middle_w - temps_w - margin
half_h = (panel_height - gap) / 2
screen.add FanList.new(printer, x: right_x, y: margin, w: right_w, row_h: row_h * 6 / 10, gap: gap)
screen.add ExtrudePanel.new(printer, x: right_x, y: margin + half_h + gap, w: right_w, row_h: row_h, gap: gap)

# While printing, covers all of the above (added last: on top).
screen.add PrintingModal.new(printer, height: Screen.height - StatusBar.height)

Screen.show screen
