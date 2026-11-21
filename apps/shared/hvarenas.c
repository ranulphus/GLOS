/* The hovercraft game's own arenas (hvarenas.h): harbour yards and basins. Every one is read and played by the
   host's test (tests/host/hv_test.c): each place can be reached and left, and the computer's craft score in it
   at every kind of game. */
#include "hvarenas.h"
#include "hvbasin.h"

static const char DRY_DOCK[] =          /* a long dock with a sunk floor between two quays, a ramp at each end */
    "name: The Dry Dock\n"
    "light: day\n"
    "\n"
    "############################\n"
    "#A..1111111111111111111...B#\n"
    "#A..1111111111111111111...B#\n"
    "#...^^...............^^....#\n"
    "#..a.....*....!....*....b..#\n"
    "#..........................#\n"
    "#....~~~..........~~~......#\n"
    "#..d.....*.........*....c..#\n"
    "#..........................#\n"
    "############################\n";

static const char TIMBER_YARD[] =       /* stacks of timber make lanes, each wide enough for two craft to pass; no water */
    "name: The Timber Yard\n"
    "light: dusk\n"
    "\n"
    "##########################\n"
    "#A........a......b......B#\n"
    "#A......................B#\n"
    "#...##..##..##..##..##...#\n"
    "#...##..##..##..##..##...#\n"
    "#....*.......!.......*...#\n"
    "#........................#\n"
    "#...##..##..##..##..##...#\n"
    "#...##..##..##..##..##...#\n"
    "#........................#\n"
    "#....*......d....c...*...#\n"
    "##########################\n";

static const char LOCK_GATES[] =        /* two basins joined by narrow gates, water along the middle */
    "name: The Lock Gates\n"
    "light: day\n"
    "\n"
    "##########################\n"
    "#A....a.....##.....b....B#\n"
    "#A..........##..........B#\n"
    "#....*....~.##.~....*....#\n"
    "#.........~....~.........#\n"
    "#.........~.!..~.........#\n"
    "#....*....~.##.~....*....#\n"
    "#...........##...........#\n"
    "#.....d.....##.....c.....#\n"
    "##########################\n";

static const char CUSTOMS_QUAY[] =      /* a raised quay down the middle, crossed by ramps; the lamp on top of nothing: at its foot */
    "name: The Customs Quay\n"
    "light: night\n"
    "\n"
    "############################\n"
    "#A.....a.....11.....b.....B#\n"
    "#A..........>11<..........B#\n"
    "#....*.......11.......*....#\n"
    "#............11............#\n"
    "#......~~...!......~~......#\n"
    "#............11............#\n"
    "#....*.......11.......*....#\n"
    "#...........>11<...........#\n"
    "#......d.....11.....c......#\n"
    "############################\n";

static const char ROUNDHOUSE[] =        /* a ring round a walled middle with a pool in it */
    "name: The Roundhouse\n"
    "light: dusk\n"
    "\n"
    "########################\n"
    "#A.....a........b.....B#\n"
    "#A....................B#\n"
    "#....*..########..*....#\n"
    "#.......#..~~..#.......#\n"
    "#..........~~..........#\n"
    "#.......#..!...#.......#\n"
    "#....*..########..*....#\n"
    "#......................#\n"
    "#......d........c......#\n"
    "########################\n";

const char *const hv_arenas[HV_ARENAS] = { HV_BASIN, DRY_DOCK, TIMBER_YARD, LOCK_GATES, CUSTOMS_QUAY, ROUNDHOUSE };
