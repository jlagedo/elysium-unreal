"""Counterexamples to zero-X-only sign canonicalization, following UE's shader equations."""

import numpy as np



def test_zero_determinant_packs_positive_sign_but_pixel_interpolation_can_observe_it():
    # RenderMath.h GetBasisDeterminantSign; a zero X basis always canonicalizes to +1.
    x, z, authored_sign = np.zeros(3), np.array([0., 0., 1.]), -1.
    y = np.cross(z, x) * authored_sign
    packed_sign = -1. if np.linalg.det(np.stack([x, y, z])) < 0 else 1.
    assert packed_sign == 1.
    # GPU VF passes sign independently to the pixel interpolants. MaterialTemplate
    # assembles cross(interpolated N, interpolated X) * interpolated W there.
    barycentric = np.array([.5, .25, .25])
    vertex_x = np.array([[0., 0., 0.], [1., 0., 0.], [1., 0., 0.]])
    pixel_x = barycentric @ vertex_x
    original_w = barycentric @ np.array([-1., 1., 1.])
    canonical_w = barycentric @ np.ones(3)
    assert not np.array_equal(np.cross(z, pixel_x) * original_w, np.cross(z, pixel_x) * canonical_w)


def test_unmirror_shader_consumer_observes_raw_sign_even_when_every_x_is_zero():
    # MaterialTemplate.ush UnMirror: Coordinate * Parameters.UnMirrored * 0.5 + 0.5.
    coordinate = .2
    assert coordinate * -1. * .5 + .5 != coordinate * 1. * .5 + .5
