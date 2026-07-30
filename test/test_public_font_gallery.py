import unittest

from scripts.generate_public_font_gallery import (
    DEVICE_CATEGORY_LABELS,
    all_families,
    categorize_family,
    device_category_groups,
    load_config,
    source_group_families,
)


class PublicFontGalleryTest(unittest.TestCase):
    def test_categories_match_device_picker_examples(self):
        expectations = {
            "Merriweather": "Serif",
            "NV Jost": "Sans Serif",
            "IBMPlexMono": "Mono / Typewriter",
            "OpenDyslexic": "Accessibility",
            "GreatVibes": "Handwritten / Script",
            "Cinzel": "Decorative",
            "UnifrakturMaguntia": "Decorative",
        }
        for family, expected in expectations.items():
            with self.subTest(family=family):
                self.assertEqual(categorize_family(family), expected)

    def test_every_public_family_appears_once(self):
        families = all_families(load_config())
        groups = device_category_groups(families)

        self.assertEqual(tuple(groups), DEVICE_CATEGORY_LABELS)
        grouped_families = [family for category in groups.values() for family in category]
        self.assertCountEqual(grouped_families, families)
        self.assertEqual(len(grouped_families), len(set(grouped_families)))

    def test_e_reader_optimized_section_uses_complete_source_group(self):
        config = load_config()
        optimized_families = source_group_families(config, "ebook-fonts")

        self.assertEqual(len(optimized_families), 37)
        self.assertIn("NV Bitter", optimized_families)
        self.assertIn("Readerly", optimized_families)


if __name__ == "__main__":
    unittest.main()
