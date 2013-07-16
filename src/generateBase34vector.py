
def int2baseVector(x, base):
    if x==0: return [0]
    digits = []
    while x:
        digits.insert(0, x % base)
        x /= base
    digits.reverse()
    return digits

L = []
MAX_STRING_BASE_256 = 21
for i in range(MAX_STRING_BASE_256):
    L.append(int2baseVector(pow(256, i), 34))

maxSize = len(L[-1])+1 # +1 for the "retenue"

# add zeros to have a rectangular matrix
for i in range(MAX_STRING_BASE_256):
    while len(L[i]) < maxSize:
        L[i].append(0);

for i in range(MAX_STRING_BASE_256):
    print L[i]
