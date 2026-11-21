/* The hovercraft game's first arena, built into the program until arenas come as files (apps/hover/hvcraft.c);
   and the line the program's demonstration says of a game's state at a step, which the host's test says too
   (tests/host/hv_test.c demo KIND STEPS): what the suite expects of the real program is made on the host. */
#ifndef HVBASIN_H
#define HVBASIN_H

static const char HV_BASIN[] =
    "name: The Old Basin\n"
    "light: day\n"
    "\n"
    "########################\n"
    "#AA........11........BB#\n"
    "#A....a...>11<...b....B#\n"
    "#....*.....~~.....*....#\n"
    "#..##......~~......##..#\n"
    "#..........!...........#\n"
    "#..##......11......##..#\n"
    "#....*....>11<....*....#\n"
    "#.....d....11....c.....#\n"
    "########################\n";

#define HV_AT_LINE "HVCRAFT at %u score=%d:%d craft0=%d,%d,%d craft1=%d,%d lamp=%d own=%ld\n"
#define HV_DEMO_CRAFT 4                 /* the demonstration's craft, and its drivers' strength */
#define HV_DEMO_STRENGTH 1

#endif
