"""Map baking consumes the cooked identity/index spaces without a legacy file lookup."""
from types import SimpleNamespace

import pytest

from pipeline.unreal import model_catalogue_views as views
from elysium_pipeline.placed_models import fnv1a_32


def row(**fields):
    return SimpleNamespace(get_editor_property=lambda name: fields[name])


def test_model_paths_keep_directory_identity_and_reject_aliases():
    assert views.static_mesh_path('Models/A/door.mdl') == '/ElysiumBaked/Models/a/SM_door'
    assert views.static_mesh_path('vtmb:model:b/door') == '/ElysiumBaked/Models/b/SM_door'
    assert views.static_mesh_path('models/a/door.mdl') != views.static_mesh_path('models/b/door.mdl')
    for value in ('door', 'models/../door.mdl', 'vtmb:texture:a/door', ''):
        with pytest.raises(ValueError):
            views.static_mesh_path(value)


def test_rest_selection_preserves_candidate_indices_even_for_repeated_labels():
    clips = [row(label='idle', weight=1, sequence='/first', base_cell=''),
             row(label='idle', weight=3, sequence='/second', base_cell='')]
    model = row(asset_id='vtmb:model:a/door', model_path='models/a/door.mdl', acceptance_issues=[],
                clips=clips, rest_candidates=[0, 1], source_absent=False, static_rest_suffices=False,
                has_cloth=True, skeletal_mesh='/mesh', static_mesh='/static')
    data = views.placed_view(row(data=row(models={'vtmb:model:a/door': model})))['vtmb:model:a/door']
    assert data['hasCloth'] and not data['staticRestSuffices']
    for token in range(50):
        expected = '/first' if fnv1a_32(data['model'], token) % 4 == 0 else '/second'
        assert views.select_rest(data, token)['sequence'] == expected


def test_all_skin_families_and_static_skeletal_routes_survive():
    id = 'vtmb:model:a/door'
    def representation(kind, prefix):
        return row(kind=kind, slots=[row(index=0, slot_name='face', skin_references=[2])],
                   families=[row(index=i, cells=[row(skin_reference=2, material=prefix+str(i))]) for i in range(3)])
    model = row(asset_id=id, family_count=3, representations=[representation('static','/static_'), representation('skeletal','/skinned_')])
    result = views.skin_view(row(data=row(models={id: model})))
    assert result[id,'static'][0] == [(0,'face','/static_0')]
    assert result[id,'skeletal'][2] == [(0,'face','/skinned_2')]


def test_conflicting_render_columns_and_missing_catalogues_fail():
    id = 'vtmb:model:a/door'
    rep = row(kind='static', slots=[row(index=0, slot_name='face', skin_references=[0,1])],
              families=[row(index=0,cells=[row(skin_reference=0,material='/a'),row(skin_reference=1,material='/b')])])
    model = row(asset_id=id, family_count=1, representations=[rep])
    with pytest.raises(ValueError,match='conflicting'):
        views.skin_view(row(data=row(models={id:model})))
    for reader in (views.skin_view, views.placed_view):
        with pytest.raises(ValueError, match='absent'):
            reader(None)


def test_map_view_does_not_resolve_unselected_native_model_references():
    def forbidden(name):
        raise AssertionError("unselected native fields must not be resolved: " + name)
    asset = row(data=row(models={'vtmb:model:a/other': SimpleNamespace(get_editor_property=forbidden)}))
    assert views.placed_view(asset, {'vtmb:model:b/selected'}) == {}
    assert views.skin_view(asset, {'vtmb:model:b/selected'}) == {}
