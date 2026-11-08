"""DWG workflow integration; synthetic data only, with optional real ODA conversion."""
import FreeCAD
import Part

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, MagicMock

module_directory = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(module_directory))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import DXFCAD
import importDWG
from dxfcad_import import import_dxf, open as open_drawing, insert
from dxfcad_edit import drawing_roots
from dxfcad_export import export_dxf

assert Path(DXFCAD.__file__).parent == module_directory

DRAWING = ("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1015\n9\n$INSUNITS\n70\n4\n"
           "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n5\nAB\n8\n0\n"
           "10\n1\n20\n2\n30\n0\n11\n4\n21\n6\n31\n0\n0\nENDSEC\n0\nEOF\n")


class DWGImportTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=module_directory)
        self.addCleanup(self.folder.cleanup)
        self.source = Path(self.folder.name) / "drawing space \u00e9.DWG"
        self.source.write_bytes(b"AC1032 synthetic placeholder")

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)

    def converted(self, text=DRAWING):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: folder.rmdir() if folder.is_dir() else None)
        path = folder / "converted.dxf"
        path.write_text(text, encoding="utf-8")
        self.addCleanup(lambda: path.unlink(missing_ok=True))
        return path

    def test_open_preserves_provenance_snapshot_and_restore(self):
        converted = self.converted()
        with patch.object(importDWG, "convertToDxf", return_value=str(converted)) as convert:
            doc = open_drawing(str(self.source))
        convert.assert_called_once()
        staged = Path(convert.call_args.args[0])
        self.assertEqual(staged.name, "drawing.dwg")
        self.assertNotEqual(staged.parent, self.source.parent)
        self.assertFalse(staged.parent.exists())
        self.assertFalse(converted.parent.exists())
        self.assertEqual(self.source.read_bytes(), b"AC1032 synthetic placeholder")
        root = drawing_roots(doc)[0]
        self.assertEqual(root.SourceFile, str(self.source.resolve()))
        self.assertEqual(root.Label, self.source.stem)
        self.assertEqual(root.SourceUnitCode, 4)
        self.assertIn("DWG was converted", root.DisplayNotes)
        line = next(o for o in doc.Objects if "DXFData" in o.PropertiesList)
        self.assertTrue(line.Shape.isValid())
        self.assertAlmostEqual(line.Shape.Length, 5.0, places=9)
        snapshot = DXFCAD.current_drawing_bytes(root)
        saved = Path(self.folder.name) / "saved.FCStd"
        doc.saveAs(str(saved))
        FreeCAD.closeDocument(doc.Name)
        self.source.unlink()
        doc = FreeCAD.openDocument(str(saved))
        self.assertIn("DWG was converted", drawing_roots(doc)[0].DisplayNotes)
        line = next(o for o in doc.Objects if "DXFData" in o.PropertiesList)
        line.Shape = Part.Shape()
        line.touch()
        doc.recompute()
        self.assertAlmostEqual(line.Shape.Length, 5.0, places=9)
        output = Path(self.folder.name) / "exported.dxf"
        export_dxf(output, doc)
        self.assertEqual(output.read_bytes(), snapshot)

    def test_insert_and_undo(self):
        doc = FreeCAD.newDocument("Existing")
        doc.UndoMode = 1
        existing = doc.addObject("App::DocumentObjectGroup", "Keep")
        converted = self.converted()
        with patch.object(importDWG, "convertToDxf", return_value=str(converted)):
            self.assertEqual(insert(str(self.source), doc.Name), doc)
        self.assertEqual(len(drawing_roots(doc)), 1)
        doc.undo()
        self.assertEqual([o.Name for o in doc.Objects], [existing.Name])

    def test_conversion_failure_leaves_documents_unchanged(self):
        doc = FreeCAD.newDocument("Existing")
        doc.addObject("App::DocumentObjectGroup", "Keep")
        for result in (None, ""):
            with self.subTest(result=result), patch.object(importDWG, "convertToDxf", return_value=result):
                with self.assertRaisesRegex(RuntimeError, "Configure a DWG converter"):
                    import_dxf(self.source, doc)
                with self.assertRaises(RuntimeError):
                    open_drawing(str(self.source))
        self.assertEqual(list(FreeCAD.listDocuments()), [doc.Name])
        self.assertEqual([o.Name for o in doc.Objects], ["Keep"])

    def test_invalid_conversion_cleans_up_before_creating_document(self):
        converted = self.converted("invalid DXF")
        with patch.object(importDWG, "convertToDxf", return_value=str(converted)):
            with self.assertRaises(Exception):
                import_dxf(self.source)
        self.assertFalse(converted.parent.exists())
        self.assertEqual(FreeCAD.listDocuments(), {})

    def test_missing_source_does_not_invoke_converter(self):
        self.source.unlink()
        with patch.object(importDWG, "convertToDxf") as convert:
            with self.assertRaises(FileNotFoundError):
                import_dxf(self.source)
            convert.assert_not_called()

    def test_dxf_bypasses_converter(self):
        path = Path(self.folder.name) / "original.dxf"
        path.write_text(DRAWING)
        with patch.object(importDWG, "convertToDxf") as convert:
            doc = import_dxf(path)
            convert.assert_not_called()
        self.assertTrue(path.exists())
        self.assertFalse(drawing_roots(doc)[0].ImportWarnings)

    def test_progress_reports_conversion_and_import_phases_and_closes(self):
        converted = self.converted()
        progress = MagicMock(started=0)
        with patch("dxfcad_import.ImportProgress", return_value=progress), patch.object(
                importDWG, "convertToDxf", return_value=str(converted)):
            import_dxf(self.source)
        phases = [call.args[0] for call in progress.update.call_args_list]
        self.assertEqual(phases, ["Converting DWG to DXF", "Reading converted DXF records",
                                 "Preparing drawing snapshot", "Setting up display",
                                 "Setting up display", "Recomputing display geometry", "Import complete"])
        self.assertEqual(progress.population.call_args_list[-1].args, (1, 1))
        progress.close.assert_called_once()

    def test_progress_closes_on_conversion_and_population_failure(self):
        progress = MagicMock(started=0)
        with patch("dxfcad_import.ImportProgress", return_value=progress), patch.object(
                importDWG, "convertToDxf", return_value=None):
            with self.assertRaises(RuntimeError):
                import_dxf(self.source)
        progress.close.assert_called_once()
        progress.reset_mock()
        converted = self.converted()
        with patch("dxfcad_import.ImportProgress", return_value=progress), patch.object(
                importDWG, "convertToDxf", return_value=str(converted)), patch(
                "dxfcad_import.initialize_population", side_effect=RuntimeError("display failed")):
            with self.assertRaisesRegex(RuntimeError, "display failed"):
                import_dxf(self.source)
        progress.close.assert_called_once()
        self.assertEqual(FreeCAD.listDocuments(), {})

    def test_population_failure_rolls_back_and_cleans_conversion(self):
        doc = FreeCAD.newDocument("Existing")
        doc.addObject("App::DocumentObjectGroup", "Keep")
        original_undo_mode = doc.UndoMode
        converted = self.converted()
        with patch.object(importDWG, "convertToDxf", return_value=str(converted)), patch(
                "dxfcad_import.initialize_population", side_effect=RuntimeError("presentation failed")):
            with self.assertRaisesRegex(RuntimeError, "presentation failed"):
                import_dxf(self.source, doc)
        self.assertFalse(converted.parent.exists())
        self.assertEqual([o.Name for o in doc.Objects], ["Keep"])
        self.assertEqual(doc.UndoMode, original_undo_mode)

    def test_cleanup_preserves_non_temporary_dxf(self):
        converted = Path(self.folder.name) / "existing.dxf"
        converted.write_text(DRAWING)
        with patch.object(importDWG, "convertToDxf", return_value=str(converted)):
            import_dxf(self.source)
        self.assertEqual(converted.read_text(), DRAWING)

    def test_converter_returning_source_cannot_delete_it(self):
        with patch.object(importDWG, "convertToDxf", return_value=str(self.source)):
            with self.assertRaisesRegex(RuntimeError, "did not return a DXF"):
                import_dxf(self.source)
        self.assertEqual(self.source.read_bytes(), b"AC1032 synthetic placeholder")

    def test_stages_brackets_unicode_and_cleans_up_on_success(self):
        self.source = self.source.rename(self.source.with_name("drawing [1] \u00e9.DWG"))
        original = self.source.read_bytes()
        staged_paths = []
        def convert(filename):
            staged = Path(filename)
            staged_paths.append(staged)
            self.assertEqual(staged.name, "drawing.dwg")
            self.assertEqual(staged.read_bytes(), original)
            self.assertEqual(list(staged.parent.iterdir()), [staged])
            converted = staged.with_suffix(".dxf")
            converted.write_text(DRAWING)
            return str(converted)
        with patch.object(importDWG, "convertToDxf", side_effect=convert):
            doc = import_dxf(self.source)
        self.assertFalse(staged_paths[0].parent.exists())
        self.assertEqual(self.source.read_bytes(), original)
        self.assertEqual(drawing_roots(doc)[0].SourceFile, str(self.source.resolve()))

    def test_staging_cleanup_after_converter_and_parser_failures(self):
        for failure in ("exception", "empty", "invalid_dxf"):
            staged_paths = []
            def convert(filename):
                staged = Path(filename)
                staged_paths.append(staged)
                self.assertEqual(staged.read_bytes(), self.source.read_bytes())
                if failure == "exception": raise RuntimeError("Synthetic converter failure")
                if failure == "empty": return ""
                converted = staged.with_suffix(".dxf")
                converted.write_text("invalid DXF")
                return str(converted)
            with self.subTest(failure=failure), patch.object(importDWG, "convertToDxf", side_effect=convert):
                with self.assertRaises(Exception):
                    import_dxf(self.source)
            self.assertFalse(staged_paths[0].parent.exists())
            self.assertEqual(self.source.read_bytes(), b"AC1032 synthetic placeholder")
            self.assertEqual(FreeCAD.listDocuments(), {})

    @unittest.skipUnless(os.environ.get("DXFCAD_TEST_ODA") == "1", "Set DXFCAD_TEST_ODA=1 for real ODA conversion")
    def test_real_oda_dwg_versions(self):
        oda = importDWG.get_oda_converter()
        self.assertTrue(oda, "ODA converter must be installed for this opt-in test")
        # Generate our own DWGs from known coordinates, then import via FreeCAD
        # discovery/conversion. Expected geometry is the analytic 3-4-5 line.
        input_dir = Path(self.folder.name) / "input"
        input_dir.mkdir()
        # A minimal R12 input is understood by ODA without requiring the
        # subclass/table/owner structure of a complete R2000 drawing.
        oda_input = ("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n0\nENDSEC\n"
                     "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n1\n20\n2\n30\n0\n"
                     "11\n4\n21\n6\n31\n0\n0\nENDSEC\n0\nEOF\n")
        (input_dir / "synthetic.dxf").write_text(oda_input)
        for version in ("ACAD2000", "ACAD2004", "ACAD2007", "ACAD2010", "ACAD2013", "ACAD2018"):
            with self.subTest(version=version):
                output_dir = Path(self.folder.name) / version
                output_dir.mkdir()
                subprocess.run([oda, str(input_dir), str(output_dir), version,
                                "DWG", "0", "0", "*.dxf"], check=True, timeout=90)
                source = output_dir / "synthetic.dwg"
                self.assertTrue(source.is_file())
                source = source.rename(output_dir / "synthetic [1] \u00e9.dwg")
                original = source.read_bytes()
                # Explicitly select ODA in memory without changing user preferences.
                get_param = importDWG.params.get_param
                with patch.object(importDWG.params, "get_param", side_effect=lambda name: 2 if name == "DWGConversion" else get_param(name)):
                    doc = import_dxf(source)
                root = drawing_roots(doc)[0]
                self.assertEqual(root.SourceFile, str(source.resolve()))
                lines = [o for o in doc.Objects if "DXFType" in o.PropertiesList and o.DXFType == "LINE"]
                self.assertEqual(len(lines), 1)
                self.assertAlmostEqual(lines[0].Shape.Length, 5.0, places=9)
                self.assertEqual(source.read_bytes(), original)
                FreeCAD.closeDocument(doc.Name)


if __name__ == "__main__":
    unittest.main()
