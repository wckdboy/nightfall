# constraints
point a
point b
point c
point d
line(a, b)
line(b, c)
line(c, d)
line(d, a)
a = (0, 0)
horizontal(a, b)
vertical(b, c)
horizontal(c, d)
vertical(d, a)
parallel(a, b, c, d)
distance(a, b, 10)
distance(b, c, 10)

# guesses
a roughly (0.2, -0.1)
b roughly (9, 0.5)
c roughly (9.5, 9)
d roughly (0.3, 10.2)
