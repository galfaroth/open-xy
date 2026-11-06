"""Which machines a native module links when the collection doesn't fit at once."""
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tooling import native

FM, MACRO, BIG, SMALL = (SimpleNamespace(id=i) for i in ('fm2op', 'macroosc', 'big', 'small'))


def linked(machines, too_big):
    """linked_machines with GNU ld replaced: a selection overflows when too_big says so."""
    calls = []

    def link(_directory, _tools, _profile, _definitions, _machines, selected, name='module', runtime_members=()):
        calls.append(([m.id for m in selected], name))
        return '2100.0 of 2032 KiB' if too_big([m.id for m in selected]) else None
    with patch.object(native, 'link', link):
        return native.linked_machines(Path('unused'), {}, {}, machines, {}), calls


class LinkedMachines(unittest.TestCase):
    def test_a_collection_that_fits_links_everything_once(self):
        machines = [FM, MACRO, BIG, SMALL]
        result, calls = linked(machines, lambda ids: False)
        self.assertEqual(result, machines)
        self.assertEqual(calls, [(['fm2op', 'macroosc', 'big', 'small'], 'module')])

    def test_otherwise_it_links_the_official_machines_once_each_other_fits_beside_them(self):
        result, calls = linked([FM, BIG, MACRO, SMALL], lambda ids: len(ids) == 4)
        self.assertEqual(result, [FM, MACRO])
        self.assertEqual(calls[1:], [(['fm2op', 'big', 'macroosc'], 'fit'), (['fm2op', 'macroosc', 'small'], 'fit'),
                                     (['fm2op', 'macroosc'], 'module')])

    def test_a_machine_too_big_even_on_its_own_fails_by_name(self):
        # Fork policy (docs/api.md): a machine that fits only on its own is still offered;
        # one that does not fit even alone is refused by name.
        with self.assertRaisesRegex(ValueError, 'big does not fit even on its own, 2100.0 of 2032 KiB'):
            linked([FM, MACRO, BIG, SMALL], lambda ids: 'big' in ids)

    def test_a_collection_without_official_machines_must_fit_at_once(self):
        with self.assertRaisesRegex(ValueError, 'Machines exceed the shared module memory, 2100.0 of 2032 KiB'):
            linked([BIG, SMALL], lambda ids: len(ids) == 2)


if __name__ == '__main__':
    unittest.main()
