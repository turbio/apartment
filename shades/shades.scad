$fn = 60;

moist_board_height = 1.62;
moist_board_width = 22.6;

esp_height = 52;
esp_width = 28.5;
esp_depth = 13.4;

temp_sensor_width = 13;
temp_sensor_height = 16;

wall_thiccness = 2;

moist_board_y = (esp_width+wall_thiccness*2)/2 - moist_board_width/2;

/*
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

*/

shaft_radius = 6;
shaft_cutout_dist = 3;
shaft_peg_radius = shaft_cutout_dist;

module sprock(radius, teeth) {
	difference() {
		cylinder(4, r=radius);

		translate([0, 0, -1]) difference() {
			cylinder(6, r=shaft_radius);
			translate([-shaft_radius,shaft_cutout_dist,0]) cube([shaft_radius*2, 6, 6]);
		}

		for (i = [0 : teeth]) {
			rotate([0, 0, i*(360/teeth)]) translate([radius, 0, 4]) sphere(4.5/2+.1, $fn=20);
		}
	}
}

// chain gear size testers
if (false) for (i = [0 : 3]) {
	translate([i*60, 0, 0]) union() {
		radius = 20+(i/2);
		linear_extrude(height = .5) text(str(radius), halign="center", valign="center");
		difference() {
			sprock(radius, 22);
			translate([-50,-50,-1]) cube([100,100,4]);
		}
	}
}

translate([0, 40, 0]) union() {
	sprock(20, 22);
	translate([0, 0, 10]) rotate([180, 0, 180]) sprock(20, 22);
}

sprock_wall_d = 20+4.5/2+.1;
sprock_wall_thicc = 4;
sprock_height = 4*2;
sprock_height_pad = 1;

shaft_hole_pad = .03;
shaft_shaft_pad = .1;
sprock_pad = 1;

difference() {
	union() {
		translate([-(sprock_wall_d+sprock_wall_thicc), -40 + 10, -sprock_wall_thicc])
			cube([(sprock_wall_d+sprock_wall_thicc)*2, 40, sprock_height+sprock_height_pad + sprock_wall_thicc*2]);
	}

	// sprocket area
	translate([-50, 0, 0]) cube([100,100,sprock_height+sprock_height_pad]);
	translate([0, 0, 0]) cylinder(sprock_height+sprock_height_pad, r=sprock_wall_d+sprock_pad);

	// shaft
	translate([0, 0, -10+1]) cylinder(10, r=shaft_radius+shaft_hole_pad);

	// notchy thing
	translate([0, 0, 0]) cylinder(20, r=shaft_peg_radius+shaft_hole_pad);
}

translate([0,0,-30])
union() {
	difference() {
		cylinder(19, r=shaft_radius-shaft_shaft_pad);
		translate([-shaft_radius, shaft_cutout_dist, 10])
			cube([shaft_radius*2, shaft_radius, 20]);
	}
	translate([0, 0, 19]) cylinder(5, r=shaft_peg_radius-shaft_shaft_pad);
}
