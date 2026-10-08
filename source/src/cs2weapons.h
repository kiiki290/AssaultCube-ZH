// cs2weapons.h: per-weapon constants taken from Counter-Strike 2
//
// Transcribed from a data package extracted out of a real CS2 install
// (Steam appid 730, buildid 25738536, extracted 2026-10-08. See the package's
// README.md for the provenance chain and SHA256SUMS.txt for the byte-level
// origin: scripts/weapons.vdata_c).
//
// Values are stored in CS2's own units, unmodified - the conversion into
// AssaultCube spread units (x1000, derived geometrically from both engines'
// spread sampling) is applied at the point of use and is a cvar
// (sv_accuracyfactor), so this table stays a faithful transcription.
//
// Authority levels, per the package's own classification:
//   - inaccuracy/spread/recovery/recoil/maxspeed fields  = A (hard data)
//   - the *algorithm* that consumes them is read from the publicly mirrored
//     CS:GO game logic (CWeaponCSBase::GetInaccuracy / UpdateAccuracyPenalty /
//     GetRecoveryTime / Recoil, CCSPlayer::KickBack). Its field names match
//     this table exactly, including late-model ones (m_flInaccuracyJumpInitial,
//     m_nRecoveryTransitionStartBullet, m_flRecoveryTimeStandFinal), so it is
//     the same model - but it is CS:GO, and CS2 may have moved a constant.
//     Every such constant is a cvar; see config/cs2.cfg.
//
// Note on m_flInaccuracyJumpApex: present in the data (0 for every weapon we
// map), but NOT read by the model above - that model derives the airborne
// penalty from sqrt(|vel.z|) remapped onto InaccuracyJumpInitial, so the apex
// is already the zero end of that curve. It is carried here anyway so the
// table is complete if the curve ever needs revisiting.
//
// Like the rest of the engine's headers this one is not self-contained: it
// uses NUMGUNS (entity.h), so it must be included after cube.h.

#ifndef CS2WEAPONS_H
#define CS2WEAPONS_H

// CFiringModeFloat fields arrive as a pair: [0] = primary mode, [1] = the
// alternate mode (scoped for the AWP, burst for the FAMAS). Weapons whose two
// modes behave identically just repeat the value.
struct cs2guninfo
{
    float spread[2];            // m_flSpread
    float inaccStand[2];        // m_flInaccuracyStand
    float inaccCrouch[2];       // m_flInaccuracyCrouch
    float inaccMove[2];         // m_flInaccuracyMove  (penalty at full run speed)
    float inaccFire[2];         // m_flInaccuracyFire  (added per shot, decays)
    float inaccJump[2];         // m_flInaccuracyJump  (flat penalty while airborne)
    float inaccJumpInitial;     // m_flInaccuracyJumpInitial (scalar; peak of the velocity curve)
    float inaccJumpApex;        // m_flInaccuracyJumpApex (unused - see note above)
    float inaccLand[2];         // m_flInaccuracyLand (scaled by landing speed; negligible)

    float recoveryStand, recoveryCrouch;              // m_flRecoveryTimeStand / Crouch
    float recoveryStandFinal, recoveryCrouchFinal;    // *Final, -1 = not defined for this weapon
    int recoveryStartBullet, recoveryEndBullet;       // m_nRecoveryTransition*Bullet

    float recoilMag[2];         // m_flRecoilMagnitude
    float recoilMagVar[2];      // m_flRecoilMagnitudeVariance
    float recoilAngleVar[2];    // m_flRecoilAngleVariance
    int recoilSeed;             // m_nRecoilSeed (the pattern's PRNG seed)
    bool fullAuto;              // m_bIsFullAuto
    // Not just a fire-mode flag: the recoil pattern generator reads it Not just a fire-mode flag: the recoil pattern generator reads it
    // too, and full-auto weapons get a smoothed, shot-scaled pattern while the rest get
    // independent kicks. m_flRecoilAngle is 0 for every weapon mapped here, so the
    // angle the table wobbles around is the constant 0 and is not carried as a field.

    float maxspeed[2];          // m_flMaxSpeed, Source units/second - absolute, not a fraction
    int numBullets;             // m_nNumBullets (9 for the shotgun)
};

extern const cs2guninfo cs2guns[NUMGUNS];

#endif
