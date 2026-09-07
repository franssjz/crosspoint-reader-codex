import pathlib
import re
import unittest


HTML_PATH = pathlib.Path(__file__).resolve().parents[2] / "src" / "network" / "html" / "FilesPage.html"


class EpubOptimizerFallbackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = HTML_PATH.read_text(encoding="utf-8")

    def test_failed_conversion_preserves_original_path(self):
        helper = re.search(
            r"function getConvertedImageOutputPath\(path, processingError\)\s*\{(?P<body>.*?)\n\}",
            self.source,
            re.DOTALL,
        )
        self.assertIsNotNone(helper)
        body = helper.group("body")
        self.assertIn("processingError ? path", body)
        self.assertIn("path.replace", body)

    def test_references_are_renamed_only_after_success(self):
        self.assertNotRegex(self.source, r"zip\.forEach\(p\s*=>[\s\S]*?renamed\[p\]")
        self.assertIn("if (!meta.processingError && newPath !== path) renamed[path] = newPath;", self.source)
        self.assertIn("getConvertedImageOutputPath(path, meta.processingError === true)", self.source)


if __name__ == "__main__":
    unittest.main()
