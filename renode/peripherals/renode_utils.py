import os


def b2i(x):
    try:
        return int(x)
    except Exception:
        try:
            return ord(x[:1])
        except Exception:
            return 0


def tohex(data):
    try:
        return data.hex()
    except Exception:
        try:
            return "".join(["%02x" % b2i(x) for x in data])
        except Exception:
            return ""


def _ensure_dir(path):
    if not os.path.isdir(path):
        try:
            os.makedirs(path)
        except Exception:
            pass


def _write_atomic(path, data):
    try:
        tmp = path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(data)
        try:
            os.replace(tmp, path)
        except Exception:
            try:
                os.rename(tmp, path)
            except Exception:
                with open(path, "wb") as f:
                    f.write(data)
                try:
                    os.unlink(tmp)
                except Exception:
                    pass
    except Exception:
        pass


def dump_ppm_frame(outdir, frame_counter, width, height, data):
    if data is None:
        return frame_counter
    try:
        pixels = bytearray()
        it = iter(data)
        for hi, lo in zip(it, it):
            v = ((b2i(hi) << 8) | b2i(lo)) & 0xFFFF
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            pixels.extend((r, g, b))
        _ensure_dir(outdir)
        header = ("P6\n%d %d\n255\n" % (width, height)).encode()
        fname = os.path.join(outdir, "lcd_frame_%03d.ppm" % frame_counter)
        latest = os.path.join(outdir, "lcd_latest.ppm")
        with open(fname, "wb") as f:
            f.write(header + bytes(pixels))
        _write_atomic(latest, header + bytes(pixels))
        return frame_counter + 1
    except Exception:
        return frame_counter
