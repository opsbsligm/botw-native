"""Exact input and guarded projection profile for the local BOTW 1.6.0 port.

This profile is intentionally separate from newer builds' address tables. It
does not mark an unknown build compatible and never edits an NSO or a library.
"""
import hashlib
from pathlib import Path
import struct

MAIN_BASE = 0x7000000000
BUILD_ID = '8e9978d50bdd20b4c8395a106c27ffde'
PINS = {
    'main': '15cfca7b89348956f85d945fade2e215a6af5991ed1071e181f97ca72f7ae20b',
    'subsdk0': 'd22d98656a19be240b554e7db8d69126d6427232521238d665aec3509087f2cf',
    'sdk': '754cf4b581d749fbe3d457cf66497b141ef994cf592d247919d64a37dcc30d27',
}

# Offsets are specific to PINS above. Original EH extents and relative vtables
# identify these four methods; the orthographic matrix is deliberately omitted.
# Full method hashes also protect non-replaced instructions from static mods.
PROJECTION_FUNCTIONS = (
    (0x12713e0, 0x127150c, '84ceb2c88be98e07a884c249d45b1775e85b57b320d0731fad34366d93423edb'),
    (0x12718b0, 0x1271978, '33550e2959c176178a16ef523b06ed383f62a45fe6d033670ced51c269024ddc'),
    (0x1271980, 0x12719f4, '28d5b2b9a9da32c7f431716a8181cd3992316d5f9352034a922728a679bee995'),
    (0x1271fc0, 0x127206c, '7772fa5e7226784af0d9318273519189ac6d7c420b2f8625a839e456736c6d66'),
)
PROJECTION_SITES = {
    'proj_update': (0x12713f0, 0x39402008),
    'persp_matrix': (0x12718bc, 0x2d558c02),  # s2/s3, unlike newer builds' s1/s2
    'persp_matrix_offset': (0x12718c4, 0x2d569404),
    'persp_unproject_y': (0x1271998, 0xbd40ac01),
    'persp_unproject_x': (0x12719c8, 0x2d558c02),
    'frustum_lr0': (0x1271fc0, 0x2d550400),  # s0/s1, unlike newer builds' s1/s0
    'frustum_lr1': (0x1271fe4, 0x2d550c02),
    'frustum_tb0': (0x1271ff8, 0x2d540800),
    'frustum_tb1': (0x1272018, 0x2d540c02),
}

# Three independent original callers load this singleton and its active event
# at +0x1d2b8. Keep their complete local contexts, including the null checks,
# guarded against code mods. This is camera evidence, not a new pacing profile.
CAMERA_EVENT_OFFSET = 0x2cc56d8
CAMERA_EVENT_GUARDS = (
    (0x47440, 0x47470, '525e5cffd95293e497f037fa4f1f9248c1f7473977e54e78b3ebb09c5c4acfed'),
    (0x49744, 0x49774, '0b3ccdb852f9e271dc854311c7ffefe18ad3dbeb6b4265b04e4e4048474b1b36'),
    (0x98480, 0x984b0, 'cf214d050499f5d9316412e9c46b1a065c698c67615f06beb6c8709ecefd0bb9'),
)


def camera_event_sites(nso):
    if nso.build_id != BUILD_ID or CAMERA_EVENT_OFFSET % 8 or \
            not nso.data[0] <= CAMERA_EVENT_OFFSET <= nso.size - 8:
        raise ValueError('SDK7 camera event: invalid module or singleton location')
    for start, end, expected in CAMERA_EVENT_GUARDS:
        if not nso.text[0] <= start < end <= sum(nso.text) or \
                hashlib.sha256(nso.image[start:end]).hexdigest() != expected:
            raise ValueError('SDK7 camera event: context hash mismatch')
    return {'sdk7_camera_event_mgr': MAIN_BASE + CAMERA_EVENT_OFFSET}


def projection_sites():
    return {name: MAIN_BASE + offset for name, (offset, _) in PROJECTION_SITES.items()}


def projection_table(module):
    """Emit only after validating the entire methods of the actual AOT input."""
    import patches
    nso = module.nso
    if module.base != MAIN_BASE or nso.build_id != BUILD_ID:
        raise ValueError('SDK7 projection: unexpected module identity')
    for name, address in camera_event_sites(nso).items():
        if module.sites.get(name) != address:
            raise ValueError('SDK7 camera event: metadata mismatch')
    for start, end, expected in PROJECTION_FUNCTIONS:
        if not nso.text[0] <= start < end <= sum(nso.text) or \
                hashlib.sha256(nso.image[start:end]).hexdigest() != expected:
            raise ValueError('SDK7 projection: method hash mismatch')
    for name, (offset, word) in PROJECTION_SITES.items():
        if module.sites.get(name) != MAIN_BASE + offset or offset + 4 > len(nso.image) or \
                struct.unpack_from('<I', nso.image, offset)[0] != word:
            raise ValueError('SDK7 projection: site mismatch: ' + name)
    result = patches.table(projection_sites())
    if PROJECTION_SITES:
        result[MAIN_BASE + PROJECTION_SITES['persp_matrix'][0]] = _persp_matrix
        result[MAIN_BASE + PROJECTION_SITES['frustum_lr0'][0]] = patches._frustum_load(0x2d550400, (0, 1), 'lr')
    return result


def _persp_matrix(gen, address):
    import patches
    if patches._word(gen, address) != 0x2d558c02:
        raise ValueError('SDK7 projection: perspective load changed')
    return ('{ extern void rt_persp(uint64_t, uint32_t *, uint32_t *); '
            'uint32_t t = LD32(c->x[0] + 0xac), k = LD32(c->x[0] + 0xb0); '
            'rt_persp(c->x[0], &t, &k); c->v[2] = t; c->v[3] = k; }'
            f' /* {address:#x}: SDK7 perspective matrix, s2/s3 */')


def verify_main(path, base, build_id, *, raw=None):
    path = Path(path)
    claimed = bytes.fromhex(build_id)
    expected_id = bytes.fromhex(BUILD_ID).ljust(32, b'\0')
    if base != MAIN_BASE or len(claimed) > 32 or claimed.ljust(32, b'\0') != expected_id:
        raise ValueError('SDK7 profile: unexpected main base or build ID')
    if raw is None:
        raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != PINS['main']:
        raise ValueError('SDK7 profile: original main hash does not match')
    from nso import Nso
    return '1.6.0', {**projection_sites(), **camera_event_sites(Nso(path, raw=raw))}


def verify_exefs(exefs, names):
    if list(names) != list(PINS):
        raise ValueError('SDK7 profile requires exactly main, subsdk0, sdk')
    for name, expected in PINS.items():
        with (Path(exefs)/name).open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
                raise ValueError('SDK7 profile: original module hash does not match: '+name)
