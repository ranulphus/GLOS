/* Well-known DOS games by their files (gamedb.h).
 *
 * The list is in the order it is tried: a game that shares its program's name with another comes first if it
 * has the more telling companion (Final Doom runs by DOOM2.EXE, as Doom II does: its own WAD tells it). A game
 * is here only if its program's name is beyond doubt; a game not here is still found, by its folder's name
 * (apps/shared/gamelib.c), and can be given its title by hand. */
#include <stddef.h>

#include "gamedb.h"

static const struct game_known known[] = {
    /* id Software, Raven, and games on their engines */
    { "Final Doom: TNT Evilution", 1996, "TeamTNT", "Action", "DOOM2.EXE", "TNT.WAD", "SETUP.EXE" },
    { "Final Doom: The Plutonia Experiment", 1996, "id Software", "Action", "DOOM2.EXE", "PLUTONIA.WAD", "SETUP.EXE" },
    { "Doom II: Hell on Earth", 1994, "id Software", "Action", "DOOM2.EXE", "DOOM2.WAD", "SETUP.EXE" },
    { "Doom", 1993, "id Software", "Action", "DOOM.EXE", "DOOM.WAD|DOOM1.WAD", "SETUP.EXE" },
    { "Heretic", 1994, "Raven Software", "Action", "HERETIC.EXE", "HERETIC.WAD|HERETIC1.WAD", "SETUP.EXE" },
    { "Hexen: Beyond Heretic", 1995, "Raven Software", "Action", "HEXEN.EXE", "HEXEN.WAD", "SETUP.EXE" },
    { "Wolfenstein 3D", 1992, "id Software", "Action", "WOLF3D.EXE", "VSWAP.WL6|VSWAP.WL3|VSWAP.WL1", NULL },
    { "Spear of Destiny", 1992, "id Software", "Action", "SPEAR.EXE", "VSWAP.SOD", NULL },
    { "Quake", 1996, "id Software", "Action", "QUAKE.EXE", "ID1", NULL },
    { "Commander Keen 4: Secret of the Oracle", 1991, "id Software", "Platform", "KEEN4E.EXE", NULL, NULL },
    { "Commander Keen 1: Marooned on Mars", 1990, "id Software", "Platform", "KEEN1.EXE", NULL, NULL },
    /* Apogee and 3D Realms */
    { "Duke Nukem 3D", 1996, "3D Realms", "Action", "DUKE3D.EXE", "DUKE3D.GRP", "SETUP.EXE" },
    { "Shadow Warrior", 1997, "3D Realms", "Action", "SW.EXE", "SW.GRP", "SETUP.EXE" },
    { "Blood", 1997, "Monolith Productions", "Action", "BLOOD.EXE", "BLOOD.RFF", "SETUP.EXE" },
    { "Rise of the Triad", 1995, "Apogee Software", "Action", "ROTT.EXE", "DARKWAR.WAD|HUNTBGIN.WAD", NULL },
    { "Duke Nukem II", 1993, "Apogee Software", "Platform", "NUKEM2.EXE", NULL, NULL },
    { "Cosmo's Cosmic Adventure", 1992, "Apogee Software", "Platform", "COSMO1.EXE", NULL, NULL },
    { "Bio Menace", 1993, "Apogee Software", "Platform", "BMENACE1.EXE", NULL, NULL },
    { "Blake Stone: Aliens of Gold", 1993, "JAM Productions", "Action", "BSTONE.EXE", NULL, NULL },
    { "Hocus Pocus", 1994, "Moonlite Software", "Platform", "HOCUS.EXE", NULL, NULL },
    { "Raptor: Call of the Shadows", 1994, "Cygnus Studios", "Action", "RAP.EXE", NULL, "SETUP.EXE" },
    { "Terminal Velocity", 1995, "Terminal Reality", "Action", "TV.EXE", NULL, "SETUP.EXE" },
    /* Epic MegaGames */
    { "Jazz Jackrabbit", 1994, "Epic MegaGames", "Platform", "JAZZ.EXE", NULL, NULL },
    { "Jill of the Jungle", 1992, "Epic MegaGames", "Platform", "JILL1.EXE", NULL, NULL },
    { "Tyrian", 1995, "Eclipse Software", "Action", "TYRIAN.EXE", NULL, "SETUP.EXE" },
    { "One Must Fall: 2097", 1994, "Diversions Entertainment", "Action", "OMF.EXE", NULL, "SETUP.EXE" },
    /* Parallax, Looking Glass, Origin */
    { "Descent", 1995, "Parallax Software", "Action", "DESCENT.EXE", "DESCENT.HOG", "SETUP.EXE" },
    { "Descent II", 1996, "Parallax Software", "Action", "D2.EXE", "DESCENT2.HOG", "SETUP.EXE" },
    { "System Shock", 1994, "Looking Glass Technologies", "Role-playing", "CDSHOCK.EXE", NULL, NULL },
    { "Ultima Underworld: The Stygian Abyss", 1992, "Blue Sky Productions", "Role-playing", "UW.EXE", NULL, NULL },
    { "Ultima VII: The Black Gate", 1992, "Origin Systems", "Role-playing", "ULTIMA7.COM", NULL, NULL },
    { "Ultima VIII: Pagan", 1994, "Origin Systems", "Role-playing", "U8.EXE", NULL, NULL },
    { "Wing Commander", 1990, "Origin Systems", "Simulation", "WC.EXE", NULL, NULL },
    { "Wing Commander II: Vengeance of the Kilrathi", 1991, "Origin Systems", "Simulation", "WC2.EXE", NULL, NULL },
    /* LucasArts */
    { "The Secret of Monkey Island", 1990, "Lucasfilm Games", "Adventure", "MONKEY.EXE", NULL, NULL },
    { "Monkey Island 2: LeChuck's Revenge", 1991, "LucasArts", "Adventure", "MONKEY2.EXE", NULL, NULL },
    { "Indiana Jones and the Fate of Atlantis", 1992, "LucasArts", "Adventure", "ATLANTIS.EXE", NULL, NULL },
    { "Day of the Tentacle", 1993, "LucasArts", "Adventure", "TENTACLE.EXE", NULL, NULL },
    { "Sam & Max Hit the Road", 1993, "LucasArts", "Adventure", "SAMNMAX.EXE", NULL, NULL },
    { "Star Wars: X-Wing", 1993, "LucasArts", "Simulation", "XWING.EXE", NULL, NULL },
    { "Star Wars: TIE Fighter", 1994, "LucasArts", "Simulation", "TIE.EXE", NULL, NULL },
    { "Star Wars: Dark Forces", 1995, "LucasArts", "Action", "DARK.EXE", NULL, NULL },
    { "Star Wars: Rebel Assault", 1993, "LucasArts", "Action", "REBEL.EXE", NULL, NULL },
    /* strategy */
    { "Sid Meier's Civilization", 1991, "MicroProse", "Strategy", "CIV.EXE", NULL, NULL },
    { "UFO: Enemy Unknown (X-COM: UFO Defense)", 1994, "Mythos Games", "Strategy", "UFO.BAT", "GEOSCAPE.EXE", NULL },
    { "X-COM: Terror from the Deep", 1995, "MicroProse", "Strategy", "TERROR.COM", "GEOSCAPE.EXE", NULL },
    { "Master of Orion", 1993, "Simtex", "Strategy", "ORION.EXE", NULL, NULL },
    { "Master of Orion II: Battle at Antares", 1996, "Simtex", "Strategy", "ORION2.EXE", NULL, NULL },
    { "Master of Magic", 1994, "Simtex", "Strategy", "MAGIC.EXE", NULL, NULL },
    { "Transport Tycoon Deluxe", 1995, "Chris Sawyer", "Strategy", "TYCOON.EXE", "TTDX.EXE", NULL },
    { "Transport Tycoon", 1994, "Chris Sawyer", "Strategy", "TYCOON.EXE", NULL, NULL },
    { "SimCity 2000", 1993, "Maxis", "Strategy", "SC2000.EXE", NULL, NULL },
    { "SimCity", 1989, "Maxis", "Strategy", "SIMCITY.EXE", NULL, NULL },
    { "Warcraft: Orcs & Humans", 1994, "Blizzard Entertainment", "Strategy", "WAR.EXE", NULL, "SETUP.EXE" },
    { "Warcraft II: Tides of Darkness", 1995, "Blizzard Entertainment", "Strategy", "WAR2.EXE", NULL, "SETUP.EXE" },
    { "Command & Conquer", 1995, "Westwood Studios", "Strategy", "C&C.EXE", NULL, "SETUP.EXE" },
    { "Command & Conquer: Red Alert", 1996, "Westwood Studios", "Strategy", "RA.EXE", NULL, "SETUP.EXE" },
    { "Dune II: The Building of a Dynasty", 1992, "Westwood Studios", "Strategy", "DUNE2.EXE", NULL, "SETUP.EXE" },
    { "Heroes of Might and Magic", 1995, "New World Computing", "Strategy", "HEROES.EXE", NULL, NULL },
    { "Heroes of Might and Magic II", 1996, "New World Computing", "Strategy", "HEROES2.EXE", NULL, NULL },
    { "Syndicate", 1993, "Bullfrog Productions", "Strategy", "SYND.EXE", NULL, NULL },
    { "Theme Hospital", 1997, "Bullfrog Productions", "Strategy", "HOSPITAL.EXE", NULL, NULL },
    { "Dungeon Keeper", 1997, "Bullfrog Productions", "Strategy", "KEEPER.EXE", NULL, NULL },
    { "Populous", 1989, "Bullfrog Productions", "Strategy", "POPULOUS.EXE", NULL, NULL },
    { "Worms", 1995, "Team17", "Strategy", "WORMS.EXE", NULL, NULL },
    { "Cannon Fodder", 1993, "Sensible Software", "Strategy", "CANNON.EXE", NULL, NULL },
    { "Scorched Earth", 1991, "Wendell Hicken", "Strategy", "SCORCH.EXE", NULL, NULL },
    /* others */
    { "Prince of Persia", 1990, "Broderbund", "Platform", "PRINCE.EXE", NULL, NULL },
    { "Lemmings", 1991, "DMA Design", "Puzzle", "VGALEMMI.EXE", NULL, NULL },
    { "The Incredible Machine", 1993, "Dynamix", "Puzzle", "TIM.EXE", NULL, NULL },
    { "The Lost Vikings", 1993, "Silicon & Synapse", "Platform", "VIKINGS.EXE", NULL, NULL },
    { "Blackthorne", 1994, "Blizzard Entertainment", "Platform", "BTHORNE.EXE", NULL, NULL },
    { "Rayman", 1995, "Ubi Soft", "Platform", "RAYMAN.EXE", NULL, NULL },
    { "Little Big Adventure", 1994, "Adeline Software", "Adventure", "LBA.EXE", NULL, NULL },
    { "Little Big Adventure 2", 1997, "Adeline Software", "Adventure", "LBA2.EXE", NULL, NULL },
    { "Tomb Raider", 1996, "Core Design", "Action", "TOMB.EXE", NULL, NULL },
    { "Carmageddon", 1997, "Stainless Software", "Racing", "CARMA.EXE", NULL, NULL },
    { "Stunts", 1990, "Distinctive Software", "Racing", "STUNTS.COM", NULL, NULL },
    { "Formula One Grand Prix", 1992, "MicroProse", "Racing", "GP.EXE", NULL, NULL },
    { "Grand Prix 2", 1996, "MicroProse", "Racing", "GP2.EXE", NULL, NULL },
    { "MechWarrior 2: 31st Century Combat", 1995, "Activision", "Simulation", "MECH2.EXE", NULL, NULL },
    { "Eye of the Beholder", 1991, "Westwood Associates", "Role-playing", "EYE.EXE", NULL, NULL },
    { "The Elder Scrolls II: Daggerfall", 1996, "Bethesda Softworks", "Role-playing", "DAGGER.EXE", "FALL.EXE", NULL },
    /* GLOS's own and its sister projects' (they run on DOS-GL) */
    { "Fifth Wheel", 2026, "The Fifth Wheel project", "Simulation", "FWHEEL.EXE", NULL, NULL },
    { "PrBoom-plus", 0, "The PrBoom-plus team", "Action", "PRBOOMP.EXE", NULL, NULL },
};

unsigned gamedb_count(void) { return (unsigned)(sizeof known / sizeof known[0]); }
const struct game_known *gamedb_at(unsigned i) { return i < gamedb_count() ? &known[i] : NULL; }

static int same(const char *a, const char *b, unsigned nb)     /* a is b's first nb letters, whatever their case */
{
    unsigned i;
    for (i = 0; i < nb; i++) {
        int x = a[i], y = b[i];
        x = x >= 'a' && x <= 'z' ? x - 32 : x, y = y >= 'a' && y <= 'z' ? y - 32 : y;
        if (!x || x != y)
            return 0;
    }
    return !a[nb];
}

static int has(const char *const *names, unsigned n, const char *what, unsigned len)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (names[i] && same(names[i], what, len))
            return 1;
    return 0;
}

const struct game_known *gamedb_match(const char *const *names, unsigned n)
{
    unsigned i;
    for (i = 0; i < gamedb_count(); i++) {
        const char *w = known[i].with, *bar;
        unsigned len = 0;
        while (known[i].run[len])
            len++;
        if (!has(names, n, known[i].run, len))
            continue;
        if (!w)
            return &known[i];
        for (;; w = bar + 1) {                  /* any one of its companions */
            for (bar = w; *bar && *bar != '|'; bar++)
                ;
            if (has(names, n, w, (unsigned)(bar - w)))
                return &known[i];
            if (!*bar)
                break;
        }
    }
    return NULL;
}
