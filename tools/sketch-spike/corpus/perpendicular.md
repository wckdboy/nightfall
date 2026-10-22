# constraints
point p0
point p1
point p2
point p3
line(p0, p1)
line(p2, p3)
p0 = (0, 0)
p1 = (0, 4)
p2 = (0, 0)
p3.x = 4
perpendicular(p0, p1, p2, p3)

# guesses
p0 roughly (0, 0)
p1 roughly (0.1, 3.9)
p2 roughly (0, 0)
p3 roughly (3.8, 0.4)
