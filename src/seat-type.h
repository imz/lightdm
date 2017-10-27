#ifndef SEAT_TYPE_H_
#define SEAT_TYPE_H_

typedef struct SeatPrivate SeatPrivate;

typedef struct
{
    GObject      parent_instance;
    SeatPrivate *priv;
} Seat;

#endif /* SEAT_TYPE_H_ */
