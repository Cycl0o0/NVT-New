#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "filter.h"

static void test_line_sort_and_search(void)
{
    Line lines[3] = {
        {.gid = 1, .ident = 10, .active = true, .libelle = "Bus A", .vehicule = "BUS"},
        {.gid = 2, .ident = 2, .active = true, .libelle = "Tram B", .vehicule = "TRAM"},
        {.gid = 3, .ident = 50, .active = false, .libelle = "Night", .vehicule = "BUS"},
    };
    int filtered[3];

    qsort(lines, 3, sizeof(Line), nvt_cmp_lines);
    assert(strcmp(lines[0].vehicule, "TRAM") == 0);
    assert(nvt_strcasestr_s("Bordeaux", "dEaUx"));
    assert(nvt_match_offset("Lagoon Theme", "theme") == 7);
    assert(nvt_rebuild_line_filter(lines, 3, "tram", filtered, 3) == 1);
    assert(filtered[0] == 0);
}

static void test_toulouse_helpers(void)
{
    ToulouseLine lines[2] = {
        {.id = 1, .code = "A", .libelle = "Metro A", .mode = "Metro", .couleur = "#FFCC00"},
        {.id = 2, .code = "L1", .libelle = "Linéo 1", .mode = "Linéo", .couleur = "#112233"},
    };
    ToulouseStop stops[2] = {
        {.id = 1, .libelle = "Jean Jaures", .commune = "Toulouse", .lignes = "A B"},
        {.id = 2, .libelle = "Airport", .commune = "Blagnac", .lignes = "T1"},
    };
    int filtered[2];

    assert(nvt_toulouse_mode_is_rail("Métro"));
    assert(nvt_toulouse_mode_is_rail("Tram"));
    assert(nvt_toulouse_list_has_token("A B T1", "B"));
    assert(!nvt_toulouse_list_has_token("A B T1", "C"));
    assert(nvt_rebuild_toulouse_line_filter(lines, 2, "lin", filtered, 2) == 1);
    assert(filtered[0] == 1);
    assert(nvt_rebuild_toulouse_stop_filter(stops, 2, "toulouse", filtered, 2) == 1);
    assert(filtered[0] == 0);
    assert(nvt_toulouse_waiting_minutes("00:04:30") == 5);
}

static void test_idfm_line_priority(void)
{
    ToulouseLine lines[5] = {
        {.code = "350", .libelle = "Bus 350", .mode = "Bus"},
        {.code = "B", .libelle = "RER B", .mode = "RER"},
        {.code = "T3", .libelle = "Tram T3", .mode = "Tramway"},
        {.code = "14", .libelle = "Metro 14", .mode = "Metro"},
        {.code = "N", .libelle = "Train N", .mode = "Train"},
    };

    qsort(lines, 5, sizeof(ToulouseLine), nvt_cmp_idfm_lines);
    assert(strcmp(nvt_idfm_line_type_label(&lines[0]), "METRO") == 0);
    assert(strcmp(nvt_idfm_line_type_label(&lines[1]), "RER") == 0);
    assert(strcmp(nvt_idfm_line_type_label(&lines[2]), "TRAM") == 0);
    assert(strcmp(nvt_idfm_line_type_label(&lines[3]), "TRAIN") == 0);
    assert(strcmp(nvt_idfm_line_type_label(&lines[4]), "BUS") == 0);
    assert(strcmp(lines[0].code, "14") == 0);
    assert(strcmp(lines[1].code, "B") == 0);
    assert(strcmp(lines[2].code, "T3") == 0);
    assert(strcmp(lines[3].code, "N") == 0);
    assert(strcmp(lines[4].code, "350") == 0);

    {
        ToulouseLine rer = {.mode = "Rapid Transit"};
        ToulouseLine train = {.mode = "Rail Shuttle"};
        ToulouseLine ter = {.mode = "TER / Intercites"};
        ToulouseLine coach = {.mode = "Coach"};

        assert(strcmp(nvt_idfm_line_type_label(&rer), "RER") == 0);
        assert(strcmp(nvt_idfm_line_type_label(&train), "TRAIN") == 0);
        assert(strcmp(nvt_idfm_line_type_label(&ter), "TRAIN") == 0);
        assert(strcmp(nvt_idfm_line_type_label(&coach), "BUS") == 0);
    }
}

int main(void)
{
    test_line_sort_and_search();
    test_toulouse_helpers();
    test_idfm_line_priority();
    return 0;
}
