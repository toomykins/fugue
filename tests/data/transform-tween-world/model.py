"""The interpolation rule shell/animation.h implements, for checking captures (see README)."""
import math
def lerp(a, b, f):
    return [x + (y - x) * f for x, y in zip(a, b)]
def interpolate(a, b, f, linear=False):
    out = lerp(a, b, f)
    if linear:
        return out
    def parts(m):
        sx = math.hypot(m[0], m[3])
        return (math.atan2(m[3], m[0]), sx, (m[0] * m[4] - m[1] * m[3]) / sx, (m[0] * m[1] + m[3] * m[4]) / sx)
    def wrap(t):
        while t > math.pi: t -= 2 * math.pi
        while t <= -math.pi: t += 2 * math.pi
        return t
    A, B = parts(a), parts(b)
    turn = wrap(B[0] - A[0])
    if abs(turn) > math.pi - 1e-5:
        rows = wrap(math.atan2(-b[1], b[0]) - math.atan2(-a[1], a[0]))
        if abs(rows) > math.pi - 1e-5:
            return out
        turn = -math.pi if rows < 0 else math.pi
    ang = A[0] + turn * f
    sx, sy, k = (A[i] + (B[i] - A[i]) * f for i in (1, 2, 3))
    c, s = math.cos(ang), math.sin(ang)
    out[0], out[3], out[1], out[4] = sx * c, sx * s, c * k - s * sy, s * k + c * sy
    return out
def cases(events):
    """name -> (start, [(to, flags), ...]) from the world's CASE lines."""
    out = {}
    for l in open(events):
        if not l.startswith('CASE'):
            continue
        p = l.split(); n = p[1]
        if n in out:
            continue
        steps = [([float(x) for x in p[2:8]], int(p[8]))]
        start = [float(x) for x in p[10:16]]
        if len(p) > 16:
            steps.append(([float(x) for x in p[17:23]], int(p[23])))
        out[n] = (start, steps)
    return out
def pose(case, t):
    start, steps = case
    if len(steps) == 1:
        return interpolate(start, steps[0][0], t, steps[0][1] & 2)
    if t < .5:
        return interpolate(start, steps[0][0], 2 * t, steps[0][1] & 2)
    return interpolate(steps[0][0], steps[1][0], 2 * t - 1, steps[1][1] & 2)
