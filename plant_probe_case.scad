moist_board_height = 1.62;
moist_board_width = 22.6;

esp_height = 52;
esp_width = 28.5;
esp_depth = 13.4;

temp_sensor_width = 13;
temp_sensor_height = 16;

wall_thiccness = 2;

moist_board_y = (esp_width+wall_thiccness*2)/2 - moist_board_width/2;

difference() {
	cube([
		esp_depth+wall_thiccness*2,
		esp_width+wall_thiccness*2,
		esp_height+wall_thiccness*2
	]);

	translate([wall_thiccness, moist_board_y, -1])
		cube([moist_board_height,moist_board_width,4]);

	translate([wall_thiccness,wall_thiccness,wall_thiccness])
		cube([esp_depth,esp_width,100]);

	translate([
				-1,
				(esp_width+wall_thiccness*2)/2 - temp_sensor_width/2,
				esp_height-temp_sensor_height-2
		])
		cube([5, temp_sensor_width, temp_sensor_height]);
}

translate([wall_thiccness,moist_board_y,9])
	cube([2,2,2]);

translate([wall_thiccness,moist_board_y+moist_board_width-2,9])
	cube([2,2,2]);
