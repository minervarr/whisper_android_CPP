use std::env;
use std::fs;
use ab_glyph::{FontRef, Font, ScaleFont, PxScale, OutlinedGlyph};

const EM: f32 = 40.0;
const SS: usize = 8;
const RANGE: f32 = 4.0;
const AW: usize = 512;

struct Glyph {
    cp: u32,
    advance: f32,
    has: bool,
    plane: [f32; 4],
    atlas: [f32; 4],
    cell_w: usize,
    cell_h: usize,
    sdf: Vec<u8>,
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 4 {
        eprintln!("usage: atlas_gen <font> <out.msdf> <out.rgba>");
        std::process::exit(2);
    }
    let font_bytes = fs::read(&args[1]).expect("read font");
    let font = FontRef::try_from_slice(&font_bytes).expect("parse font");

    let raster_px = EM * SS as f32;
    let scale = PxScale::from(raster_px);
    let scaled = font.as_scaled(scale);

    let pad_a = RANGE / 2.0;
    let pad_m = (pad_a * SS as f32).round() as usize;

    let mut chars: Vec<char> = Vec::new();
    for c in 0x20u32..=0x7E { chars.push(char::from_u32(c).unwrap()); }
    for c in 0xA1u32..=0xFF { chars.push(char::from_u32(c).unwrap()); }
    for c in [0x152u32, 0x153, 0x178, 0x20AC] { chars.push(char::from_u32(c).unwrap()); }

    let mut glyphs: Vec<Glyph> = Vec::new();
    for ch in chars {
        let cp = ch as u32;
        let is_space = ch == ' ';
        let g_id = font.glyph_id(ch);
        if !is_space && g_id.0 == 0 {
            continue; // not in font
        }
        
        let advance = scaled.h_advance(g_id) / raster_px;
        let q_opt = scaled.outline_glyph(g_id.with_scale(scale));

        if q_opt.is_none() {
            glyphs.push(Glyph { cp, advance, has: false, plane: [0.0; 4],
                                atlas: [0.0; 4], cell_w: 0, cell_h: 0, sdf: Vec::new() });
            continue;
        }
        let q = q_opt.unwrap();
        let bounds = q.px_bounds();
        let m_width = (bounds.max.x - bounds.min.x).ceil() as usize;
        let m_height = (bounds.max.y - bounds.min.y).ceil() as usize;

        if m_width == 0 || m_height == 0 {
            glyphs.push(Glyph { cp, advance, has: false, plane: [0.0; 4],
                                atlas: [0.0; 4], cell_w: 0, cell_h: 0, sdf: Vec::new() });
            continue;
        }

        let mut cov = vec![0u8; m_width * m_height];
        q.draw(|x, y, v| {
            if (x as usize) < m_width && (y as usize) < m_height {
                cov[(y as usize) * m_width + (x as usize)] = (v * 255.0) as u8;
            }
        });

        let g_l = bounds.min.x / SS as f32;
        let g_r = bounds.max.x / SS as f32;
        let top_up = -bounds.min.y / SS as f32;
        let bot_up = -bounds.max.y / SS as f32;

        let plane_l = g_l / EM - pad_a / EM;
        let plane_t = -top_up / EM - pad_a / EM;
        let cell_w = ((g_r - g_l) + RANGE).round() as usize;
        let cell_h = ((top_up - bot_up) + RANGE).round() as usize;
        let plane_r = plane_l + cell_w as f32 / EM;
        let plane_b = plane_t + cell_h as f32 / EM;

        let sdf = build_sdf(&cov, m_width, m_height, cell_w, cell_h, pad_m);

        glyphs.push(Glyph {
            cp, advance, has: true,
            plane: [plane_l, plane_t, plane_r, plane_b],
            atlas: [0.0; 4],
            cell_w, cell_h, sdf,
        });
    }

    glyphs.sort_by_key(|g| (-(g.cell_h as isize), g.cp));

    let mut cur_x = 0;
    let mut cur_y = 0;
    let mut row_h = 0;
    let mut order = Vec::new();
    for i in 0..glyphs.len() { order.push(i); }

    for &gi in &order {
        let (cw, ch) = (glyphs[gi].cell_w, glyphs[gi].cell_h);
        if cw == 0 { continue; }
        if cur_x + cw > AW { cur_x = 0; cur_y += row_h + 1; row_h = 0; }
        glyphs[gi].atlas = [cur_x as f32, cur_y as f32,
                            (cur_x + cw) as f32, (cur_y + ch) as f32];
        cur_x += cw + 1;
        if ch > row_h { row_h = ch; }
    }
    let ah = cur_y + row_h;

    let mut atlas = vec![0u8; AW * ah * 4];
    for g in &glyphs {
        if !g.has { continue; }
        let (ox, oy) = (g.atlas[0] as usize, g.atlas[1] as usize);
        for j in 0..g.cell_h {
            for i in 0..g.cell_w {
                let v = g.sdf[j * g.cell_w + i];
                let p = ((oy + j) * AW + (ox + i)) * 4;
                atlas[p] = v; atlas[p + 1] = v; atlas[p + 2] = v; atlas[p + 3] = 255;
            }
        }
    }
    fs::write(&args[3], &atlas).expect("write rgba");

    let ascender = -(scaled.ascent() / raster_px);
    let descender = -(scaled.descent() / raster_px);
    let line_height = (scaled.ascent() - scaled.descent() + scaled.line_gap()) / raster_px;

    let mut buf: Vec<u8> = Vec::new();
    buf.extend_from_slice(&0x4644534Du32.to_le_bytes());
    buf.extend_from_slice(&(AW as u32).to_le_bytes());
    buf.extend_from_slice(&(ah as u32).to_le_bytes());
    buf.extend_from_slice(&RANGE.to_le_bytes());
    buf.extend_from_slice(&EM.to_le_bytes());
    buf.extend_from_slice(&line_height.to_le_bytes());
    buf.extend_from_slice(&ascender.to_le_bytes());
    buf.extend_from_slice(&descender.to_le_bytes());
    buf.extend_from_slice(&(glyphs.len() as u32).to_le_bytes());
    for g in &glyphs {
        buf.extend_from_slice(&g.cp.to_le_bytes());
        buf.extend_from_slice(&g.advance.to_le_bytes());
        buf.extend_from_slice(&(if g.has { 1u32 } else { 0 }).to_le_bytes());
        for v in g.plane { buf.extend_from_slice(&v.to_le_bytes()); }
        for v in g.atlas { buf.extend_from_slice(&v.to_le_bytes()); }
    }
    fs::write(&args[2], &buf).expect("write msdf");

    println!("wrote {} glyphs, atlas {}x{} ({} bytes rgba)",
             glyphs.len(), AW, ah, atlas.len());
}

fn build_sdf(cov: &[u8], cov_w: usize, cov_h: usize,
             cell_w: usize, cell_h: usize, pad_m: usize) -> Vec<u8> {
    let mw = cell_w * SS;
    let mh = cell_h * SS;
    let n = mw * mh;

    const INF: f64 = 1e20;
    let mut f_in = vec![INF; n];
    let mut f_out = vec![INF; n];
    let mut inside = vec![false; n];
    for j in 0..mh {
        for i in 0..mw {
            let cx = i as isize - pad_m as isize;
            let cy = j as isize - pad_m as isize;
            let ins = cx >= 0 && cy >= 0 && (cx as usize) < cov_w && (cy as usize) < cov_h
                && cov[cy as usize * cov_w + cx as usize] >= 128;
            let idx = j * mw + i;
            inside[idx] = ins;
            if ins { f_in[idx] = 0.0; } else { f_out[idx] = 0.0; }
        }
    }
    let d_in = edt2d(&f_in, mw, mh);
    let d_out = edt2d(&f_out, mw, mh);

    let mut out = vec![0u8; cell_w * cell_h];
    for j in 0..cell_h {
        for i in 0..cell_w {
            let mx = i * SS + SS / 2;
            let my = j * SS + SS / 2;
            let idx = my * mw + mx;
            let signed_m = if inside[idx] { d_out[idx].sqrt() } else { -d_in[idx].sqrt() };
            let signed_a = (signed_m / SS as f64) as f32;
            let v = 0.5 + signed_a / RANGE;
            out[j * cell_w + i] = (v.clamp(0.0, 1.0) * 255.0).round() as u8;
        }
    }
    out
}

fn edt2d(f: &[f64], w: usize, h: usize) -> Vec<f64> {
    let mut d = f.to_vec();
    let mut col = vec![0f64; h];
    for x in 0..w {
        for y in 0..h { col[y] = d[y * w + x]; }
        let r = edt1d(&col);
        for y in 0..h { d[y * w + x] = r[y]; }
    }
    let mut row = vec![0f64; w];
    for y in 0..h {
        for x in 0..w { row[x] = d[y * w + x]; }
        let r = edt1d(&row);
        for x in 0..w { d[y * w + x] = r[x]; }
    }
    d
}

fn edt1d(f: &[f64]) -> Vec<f64> {
    let n = f.len();
    let mut d = vec![0f64; n];
    let mut v = vec![0usize; n];
    let mut z = vec![0f64; n + 1];
    const INF: f64 = 1e20;
    let mut k = 0usize;
    v[0] = 0;
    z[0] = -INF;
    z[1] = INF;
    for q in 1..n {
        let mut s;
        loop {
            let vk = v[k] as f64;
            s = ((f[q] + (q * q) as f64) - (f[v[k]] + vk * vk)) / (2.0 * q as f64 - 2.0 * vk);
            if s <= z[k] {
                if k == 0 { break; }
                k -= 1;
            } else {
                break;
            }
        }
        k += 1;
        v[k] = q;
        z[k] = s;
        z[k + 1] = INF;
    }
    k = 0;
    for q in 0..n {
        while z[k + 1] < q as f64 { k += 1; }
        let d_ = q as f64 - v[k] as f64;
        d[q] = d_ * d_ + f[v[k]];
    }
    d
}
