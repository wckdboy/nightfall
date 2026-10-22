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
distance(a, b, 30)
lines_equal_length(a, b, b, c)

# guesses
a roughly (0, 0)
b roughly (28, 1)
c roughly (29, 31)
d roughly (1, 29)
