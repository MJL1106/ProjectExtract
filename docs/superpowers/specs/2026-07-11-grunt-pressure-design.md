# Grunt Pressure Design

## Goal

Reduce officerless grunt pressure without weakening awareness or changing the already-working officer-led combat.

## Behaviour

- Shared squad sightings and shared target focus remain unchanged.
- A grunt may only execute the deliberate flank task while its squad has a living officer.
- Officerless grunts may still advance, but each Press is a short episode rather than a permanent posture.
- A Press episode permits at most one committed advance, then enters a randomized recovery before another Press opportunity.
- A Press episode also ends if its maximum duration expires, morale stops being Confident, or suppression interrupts it.
- Grunt advance candidates closer than the configured minimum threat distance are rejected.
- Rusher behavior and officer-led bounding, focus, rally, and coordination remain unchanged.

## Initial Grunt Tuning

- Initial Press opportunity delay: 4–8 seconds after combat begins.
- Maximum Press episode: 6 seconds.
- Recovery after a Press or interruption: 10–16 seconds.
- Minimum advance distance from the threat: 800 cm.
- Existing shared awareness, target selection, fire cadence, and move-and-shoot behavior remain unchanged.

## Verification

- A grunt-only squad shares awareness but cannot claim the deliberate flank branch.
- Grunts advance independently, stop outside 800 cm, and return to Hold after one committed advance.
- A living officer restores the existing flank and coordinated maneuver behavior.
- Killing the officer prevents new deliberate flank tasks without clearing shared awareness.

