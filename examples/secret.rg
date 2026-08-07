module secret

# --- the surface other modules are ALLOWED to touch ---
pub const shown = 1

# --- module-private: legal ONLY inside module 'secret' ---
internal const hidden = 42
