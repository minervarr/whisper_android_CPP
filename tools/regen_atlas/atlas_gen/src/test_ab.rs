use ab_glyph::{FontRef, Font, ScaleFont, PxScale};

fn main() {
    let font_bytes = std::fs::read(r"../../../app/src/main/assets/fonts/font.otf").unwrap();
    let font = FontRef::try_from_slice(&font_bytes).unwrap();
    let scale = PxScale::from(320.0);
    let scaled = font.as_scaled(scale);
    
    for ch in ['i', 'j', 'd', 'e', 'l', '1'] {
        let g = font.glyph_id(ch).with_scale(scale);
        let advance = scaled.h_advance(g.id);
        let q = scaled.outline_glyph(g);
        println!("char '{}': advance={}, bounds={:?}", ch, advance, q.map(|x| x.px_bounds()));
    }
}
