fn main() {
    let b = std::fs::read(r"../../../app/src/main/assets/fonts/font.msdf.ascii.bak").unwrap();
    let aw = u32::from_le_bytes(b[4..8].try_into().unwrap());
    let ah = u32::from_le_bytes(b[8..12].try_into().unwrap());
    println!("aw={}, ah={}", aw, ah);
}
