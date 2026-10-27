#ifndef TOWER_H
#define TOWER_H

class towerclass
{
public:
    towerclass(void);

    int backat(int xp, int yp, int yoff);

    int at(int xp, int yp, int yoff);

    /* Contiguous x=0..39 tiles, resolving vertical wrapping once per row. */
    const short* row(int yp, int yoff) const;
    const short* backrow(int yp, int yoff) const;

    int miniat(int xp, int yp, int yoff);

    void loadminitower1(void);

    void loadminitower2(void);

    void loadbackground(void);

    void loadmap(void);

    short back[40 * 120];
    short contents[40 * 700];
    short minitower[40 * 100];

    bool minitowermode;
};





#endif /* TOWER_H */
