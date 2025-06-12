#ifndef GIC_H
#define GIC_H
void gic_init(void);
unsigned int gic_acknowledge(void);
void gic_eoi(unsigned int id);
#endif