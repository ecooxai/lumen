"""Hoist invariant coverage/premultiplication from horizontal rectangle spans."""
from pathlib import Path
import hashlib
import json


def prepare(source):
    old = '    if (!round && !c->round) {\n        bool opaque = (solid >> 24) == 255;\n        int ax = LMAX(ix0, (int)ceilf(x0)), bx = LMIN(ix1, (int)floorf(x1)); if (bx < ax) bx = ax;\n        for (int y = iy0; y < iy1; y++) {\n            uint32_t *row = r->px + (size_t)y * (size_t)W;\n            float cy = span_cov(y0, y1, y);\n            bool full = cy >= 0.999f;\n            for (int x = ix0; x < ix1; x++) {\n                if (full && x == ax) {\n                    if (opaque) for (; x < bx; x++) row[x] = solid; else for (; x < bx; x++) over(&row[x], solid);\n                    if (x >= ix1) break;\n                }\n                float cov = cy * span_cov(x0, x1, x);\n                if (cov <= 0) continue;\n                over(&row[x], cov >= 0.999f ? solid : premul(col, cov));\n            }\n        }\n        return;\n    }\n'
    new = '    if (!round && !c->round) {\n        int ax = LMAX(ix0, (int)ceilf(x0)), bx = LMIN(ix1, (int)floorf(x1)); if (bx < ax) bx = ax;\n        for (int y = iy0; y < iy1; y++) {\n            uint32_t *row = r->px + (size_t)y * (size_t)W;\n            float cy = span_cov(y0, y1, y);\n            /* Horizontal interior pixels share exactly the same coverage,\n               including fractional top/bottom edges during smooth scrolling. */\n            uint32_t span = cy >= 0.999f ? solid : premul(col, cy);\n            int x = ix0;\n            for (; x < ax; x++) {\n                float cov = cy * span_cov(x0, x1, x);\n                if (cov > 0) over(&row[x], cov >= 0.999f ? solid : premul(col, cov));\n            }\n            uint32_t alpha = span >> 24;\n            if (alpha == 255) for (; x < bx; x++) row[x] = span;\n            else if (span) {\n                uint32_t k = 256 - alpha - (alpha >> 7);\n                for (; x < bx; x++) row[x] = span + scale_px(row[x], k);\n            } else x = bx;\n            for (; x < ix1; x++) {\n                float cov = cy * span_cov(x0, x1, x);\n                if (cov > 0) over(&row[x], cov >= 0.999f ? solid : premul(col, cov));\n            }\n        }\n        return;\n    }\n'
    if source.count(old) != 1:
        raise RuntimeError("Rectangle-span source anchor absent or ambiguous")
    return source.replace(old, new, 1)


if __name__ == "__main__":
    path = Path("src/paint/raster.c")
    before = path.read_text()
    after = prepare(before)
    path.write_text(after)
    print(json.dumps({"path": str(path), "before_sha256": hashlib.sha256(before.encode()).hexdigest(), "after_sha256": hashlib.sha256(after.encode()).hexdigest(), "scope": "Exact constant-coverage spans; rounded paths, coordinates, input and frame cadence unchanged."}))
