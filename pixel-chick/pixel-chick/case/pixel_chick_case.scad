// Pixel Chick - chubby chick case for the OLED keychain (OpenSCAD)
//
// Render one part at a time and export it as STL:
//   PART = "body"   -> front shell: screen window, crest with keyring hole, wings   (YELLOW)
//   PART = "lid"    -> snap-in back lid                                             (YELLOW)
//   PART = "foot"   -> foot, glued under the body, toes stick out at the front      (ORANGE, x2)
//   PART = "cap"    -> cap that goes on the push button                             (ORANGE)
//   PART = "cheek"  -> cheek, glued into its pocket                                 (PINK, x2)
//   PART = "assembly" -> coloured preview of everything (not for printing)
//
// Command line example:
//   openscad -D 'PART="body"' -o body.stl pixel_chick_case.scad
PART = "body";
LID_OFF = false;     // assembly preview only: hide the lid to see the inside
$fn = 64;

// ---------- Main sizes (mm) ----------
// EXTRA_DEPTH makes the case deeper without changing its outline.
// The first print at EXTRA_DEPTH = 0 was too tight with all the wiring inside;
// the build in the photos was reprinted deeper (exact amount not measured).
// 5 mm is an estimate from the parts stack: boards 14.4 mm + wire bundle ~4.5 mm + battery 6.2 mm
// = ~25 mm, plus ~1.5 mm spare. Too deep is harmless (hot glue holds the parts); too shallow is not.
EXTRA_DEPTH = 5;

IA = 33; IB = 29; N = 3;    // inner cavity half-width, half-height and "chubbiness" (bigger N = squarer)
CY = 22;                    // vertical centre of the body
WALL      = 1.8;            // side wall
FRONT     = 1.6;            // front face thickness
INNER_D   = 21.5 + EXTRA_DEPTH;  // inner depth (screen + boards + battery)
LID       = 2.0;            // lid plate thickness
LIP       = 2.2;            // height of the lid lip that goes inside the body
CLEARANCE = 0.25;           // lid fit: too tight -> 0.35, too loose -> 0.15 (reprint only the lid)
FILLET    = 2.6;            // rounding of the front and back edges
BODY_D = FRONT + INNER_D;

// ---------- Screen (0.96" SSD1306 module) ----------
OLED_W = 27.6; OLED_H = 28.0; OLED_CY = 26;
// Window: sized to the display glass only (second reprint). Measured from photos of the
// printed case, about +/-0.5 mm. The first version used 25.6 x 26.0 centred on the module,
// which also showed the module's PCB edges above and below the glass.
WIN_W = 25.0; WIN_H = 16.3; WIN_CY = 29.9;

// ---------- Boards ----------
TP_W = 17.2; TP_L = 28.2; TP_X0 = 10.8; TP_Y0 = 2.3;     // TP4056 charger, USB-C facing down
PM_L = 39.2; PM_W = 18.2; PM_X0 = -28.8; PM_Y0 = 23.5;   // Pro Mini incl. right-angle header
BOARD_Z = FRONT + 8.0;                                     // depth of the board layer
USB_W = 9.6; USB_H = 4.2; USB_Z = BOARD_Z + 3.2;           // USB-C opening

// ---------- Button and power switch (top of the head) ----------
BTN_X = 15.5; BTN_Z = BOARD_Z + 3.5; BTN_D = 4.0;   // 6x6x8 push button, round hole
SW_X  = 24.0; SW_Z  = BOARD_Z + 3.5; SW_A  = 4.2;   // self-locking push switch, square hole

// ---------- Decorations ----------
CREST_T = 6; RING_R = 2.4;              // crest thickness, keyring hole radius (4.8 mm hole)
CHEEK_X = 19; CHEEK_Y = 10.8;
FOOT_X = 8; FOOT_IN = 6;                // feet are glued under the body, toes stick out 3-4 mm

// ========== 2D shapes ==========
function se(a, b, n, t) = [a * sign(cos(t)) * pow(abs(cos(t)), 2/n), b * sign(sin(t)) * pow(abs(sin(t)), 2/n)];
module cavity_raw(e=0) { translate([0, CY]) polygon([for (t=[0:2:358]) se(IA + e, IB + e, N, t)]); }
module wings2d() {
    for (s=[-1,1]) translate([s*(IA + 2.0), CY - 4]) rotate(s*25) scale([4.0, 8.5]) circle(1);
}
// outline with a flat bottom so it can stand on a table
module outer2d(e=0) { offset(r=2.5) offset(r=-2.5) intersection() { offset(r = WALL + e) cavity_raw(); translate([-100, -e]) square([200, 200]); } }
module inner2d(e=0) { offset(r=1.5) offset(r=-1.5) intersection() { cavity_raw(e); translate([-100, WALL - e]) square([200, 200]); } }
module body2d(e=0)  { union() { outer2d(e); offset(e) wings2d(); } }

module crest2d() {
    difference() {
        union() {
            translate([0, CY + IB + WALL + 4.5]) scale([5.2, 8]) circle(1);
            for (s=[-1,1]) translate([s*6, CY + IB + WALL + 1.5]) rotate(s*-35) scale([3.4, 6.5]) circle(1);
        }
        translate([0, CY + IB + WALL + 6.5]) circle(RING_R);
    }
}

// solid with a rounded bottom edge built from thin layers (works for concave outlines too)
module rounded_slab(h, f) {
    n = 10;
    for (i=[0:n-1]) {
        z0 = f*i/n; z1 = f*(i+1)/n; zm = (z0 + z1)/2;
        e = -(f - sqrt(max(0, f*f - (f - zm)*(f - zm))));
        translate([0,0,z0]) linear_extrude(z1 - z0 + 0.001) body2d(e);
    }
    translate([0,0,f]) linear_extrude(h - f) body2d();
}

module foot2d() {   // tiny three-toed foot
    hull() { translate([0, 3]) circle(2.2); translate([0, 0]) circle(1.2); }
    for (a=[-38, 0, 38]) hull() { circle(1.2); rotate(a) translate([0, -4.6]) circle(1.25); }
}
module foot_place(s) { translate([s*FOOT_X, 0, FOOT_IN]) rotate([90,0,0]) translate([0,-3.5,0]) children(); }

// ========== Front shell ==========
module body() {
    difference() {
        union() {
            rounded_slab(BODY_D, FILLET);
            linear_extrude(CREST_T) crest2d();
        }
        translate([0,0,FRONT]) linear_extrude(BODY_D + 1) inner2d();
        translate([0, CY + IB + WALL + 6.5, -1]) cylinder(r=RING_R, h=CREST_T + 2);
        // screen window with a soft chamfer
        translate([0, WIN_CY, -0.01]) {
            linear_extrude(FRONT + 0.02) offset(3) offset(-3) square([WIN_W, WIN_H], center=true);
            linear_extrude(0.7, scale=[WIN_W/(WIN_W+2), WIN_H/(WIN_H+2)]) offset(4) offset(-4) square([WIN_W+2, WIN_H+2], center=true);
        }
        // cheek pockets (0.6 mm)
        for (s=[-1,1]) translate([s*CHEEK_X, CHEEK_Y, -0.01]) linear_extrude(0.6) scale([4.2, 2.8]) circle(1);
        // foot pockets in the bottom face (1.0 mm)
        for (s=[-1,1]) foot_place(s) translate([0,0,-1.0]) linear_extrude(1.01) offset(0.15) foot2d();
        // wing lines
        for (s=[-1,1]) translate([s*(IA + 2.0), CY - 4, -0.01]) rotate(s*25) linear_extrude(0.5)
            difference() { scale([2.6, 6.8]) circle(1); scale([1.9, 6.1]) circle(1); }
        // USB-C opening (flat bottom, right side) + recess for the plug
        translate([TP_X0 + TP_W/2, -0.01, USB_Z]) {
            rotate([-90,0,0]) linear_extrude(WALL + 1) offset(1.4) square([USB_W-2.8, USB_H-2.8], center=true);
            rotate([-90,0,0]) linear_extrude(1.1) offset(2) square([12.4-4, 7.0-4], center=true);
        }
        // button and switch holes on top
        translate([BTN_X, CY, BTN_Z]) rotate([-90,0,0]) cylinder(d=BTN_D, h=40);
        translate([SW_X,  CY, SW_Z])  rotate([-90,0,0]) linear_extrude(40) square([SW_A, SW_A], center=true);
    }
    // L-shaped corner tabs that hold the OLED module
    intersection() {
        translate([0,0,FRONT]) linear_extrude(3.2) inner2d();
        translate([0, OLED_CY, FRONT]) linear_extrude(3.2)
            for (sx=[-1,1]) for (sy=[-1,1]) scale([sx,sy])
                translate([OLED_W/2 + 0.2, OLED_H/2 + 0.2]) difference() {
                    translate([-4, -4]) square([5.2, 5.2]);
                    translate([-5, -5]) square([5, 5]);
                }
    }
}

module lid() {
    rounded_slab(LID, LID - 0.4);
    translate([0,0,LID]) linear_extrude(LIP) difference() { inner2d(-CLEARANCE); inner2d(-CLEARANCE - 1.2); }
}

// ---------- small parts ----------
module foot()  { linear_extrude(1.2) foot2d(); }
module cheek() { linear_extrude(0.6 + 0.5) scale([4.05, 2.65]) circle(1); }
module cap() {
    difference() {
        union() { cylinder(d=6.6, h=3.0); translate([0,0,3.0]) sphere(d=6.6, $fn=48); }
        translate([0,0,-0.01]) cylinder(d=3.7, h=2.4);
        translate([0,0,-5]) cube(10, center=true);
    }
}

module assembly() {
    color("#FFD43B", LID_OFF ? 0.35 : 1) body();
    if (!LID_OFF) color("#FFD43B") translate([0,0,BODY_D + LID]) mirror([0,0,1]) lid();
    for (s=[-1,1]) color("DarkOrange") foot_place(s) translate([0,0,-1.0]) foot();
    for (s=[-1,1]) color("HotPink") translate([s*CHEEK_X, CHEEK_Y, 0.6]) mirror([0,0,1]) cheek();
    color("Black") translate([-26.7/2, OLED_CY + OLED_H/2 - 1.2 - 19.3, FRONT - 0.01]) cube([26.7, 19.3, 0.2]);
    color("RoyalBlue") translate([-OLED_W/2, OLED_CY - OLED_H/2, FRONT + 0.2]) cube([OLED_W, OLED_H, 1.6]);
    color("White") for (s=[-1,1]) translate([s*5.5, WIN_CY + 1.5, FRONT - 0.05]) cylinder(d=5, h=0.1);
    color("DarkOrange") translate([BTN_X, CY + IB + 0.3, BTN_Z]) rotate([-90,0,0]) cap();
    if (LID_OFF) {
        color("SteelBlue") translate([TP_X0, TP_Y0, BOARD_Z]) cube([TP_W, TP_L, 1.6]);
        color("Silver") translate([TP_X0 + TP_W/2 - 4.5, TP_Y0 - 0.5, BOARD_Z + 1.6]) cube([9, 7.3, 3.2]);
        color("MidnightBlue") translate([PM_X0, PM_Y0, BOARD_Z]) cube([PM_L - 6, PM_W, 1.6]);
        color("Black") translate([PM_X0 + PM_L - 6, PM_Y0 + 1.5, BOARD_Z + 1.6]) cube([6, PM_W - 3, 2.5]);
        color("Khaki") translate([-55/2, CY - 35/2, FRONT + INNER_D - 6.5]) cube([55, 35, 6.2]);
        color("DimGray") translate([BTN_X - 3, 43.0, BTN_Z - 3]) cube([6, 6.2, 6]);
        color("White") translate([SW_X - 3, 41.5, SW_Z - 3]) cube([6, 6.2, 6]);
    }
}

if (PART == "body") body();
else if (PART == "lid") lid();
else if (PART == "foot") foot();
else if (PART == "cheek") cheek();
else if (PART == "cap") cap();
else assembly();
