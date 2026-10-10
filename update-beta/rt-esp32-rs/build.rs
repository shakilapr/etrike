fn main() {
    linker_be_nice();
    // Make sure linkall.x is the last linker script.
    println!("cargo:rustc-link-arg=-Tlinkall.x");
}

fn linker_be_nice() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() > 1 {
        let kind = &args[1];
        let what = &args[2];
        match kind.as_str() {
            "undefined-symbol" => match what.as_str() {
                what if what.starts_with("_defmt_") => {
                    eprintln!("?? `defmt` not found - add `defmt.x` as a linker script.");
                }
                "_stack_start" => {
                    eprintln!("?? Is the linker script `linkall.x` missing?");
                }
                _ => (),
            },
            _ => std::process::exit(1),
        }
        std::process::exit(0);
    }
    println!(
        "cargo:rustc-link-arg=-Wl,--error-handling-script={}",
        std::env::current_exe().unwrap().display()
    );
}
