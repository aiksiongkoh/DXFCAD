"""FreeCAD transactions and ownership for the native C++ editing APIs."""

from contextlib import contextmanager

import FreeCAD


@contextmanager
def transaction(document, label):
    # Editing must remain undoable in console-created documents too.
    if document.UndoMode == 0:
        document.UndoMode = 1
    document.openTransaction(label)
    try:
        yield
        document.recompute()
        document.commitTransaction()
    except Exception:
        document.abortTransaction()
        document.recompute()
        raise


def drawing_roots(document):
    return [obj for obj in document.Objects if "DXFSourceData" in obj.PropertiesList]


def drawing_root(obj):
    if "DXFSourceData" in obj.PropertiesList:
        return obj
    return getattr(obj, "DXFSourceRoot", None)


def _check_display(obj):
    obj.recompute()
    if obj.DisplayStatus.startswith("Error:"):
        raise ValueError(obj.DisplayStatus)


def apply_polyline(obj, polyline, origins=None):
    import DXFCAD

    plan = DXFCAD.prepare_polyline_edit(obj, polyline, [] if origins is None else origins)
    apply_entity_update(plan, "Edit DXF LWPolyline")
    return obj


def create_polyline(document, polyline, root=None):
    import DXFCAD

    return _create_entity(document, DXFCAD.create_lwpolyline(polyline), root, "LWPolyline")


def create_text(document, content, height, origin, multiline=False, width=0, root=None):
    import DXFCAD
    from dxfcad_import import default_text_font
    raw = DXFCAD.create_text(content,height,origin,multiline,width)
    return _create_entity(document,raw,root,"MTEXT" if multiline else "TEXT",font=default_text_font())


def create_rectangle(document, first, opposite, root=None):
    import DXFCAD
    return _create_entity(document,DXFCAD.create_rectangle(first,opposite),root,"Rectangle")


def create_circle(document, center, radius, root=None):
    import DXFCAD
    return _create_entity(document, DXFCAD.create_circle(center, radius), root, "CIRCLE")


def create_arc(document, center, radius, start_degrees, end_degrees, clockwise=False, root=None):
    import DXFCAD
    return _create_entity(document,DXFCAD.create_arc(center,radius,start_degrees,end_degrees,clockwise),root,"Arc")


def create_arc_through_points(document, start, through, end, root=None):
    import DXFCAD
    return _create_entity(document,DXFCAD.create_arc_through_points(start,through,end),root,"Arc")


def create_line(document, start, end, root=None):
    import DXFCAD

    return _create_entity(document, DXFCAD.create_line(start, end), root, "LINE")


def _create_entity(document, raw, root, kind, *, font=""):
    import DXFCAD
    from dxfcad_objects import DXFObjectProxy, DXFViewProvider

    group_names = {obj.Name for obj in document.Objects if "Group" in obj.PropertiesList}
    with transaction(document, "Create DXF "+kind):
        obj = DXFCAD.create_entity(document, raw, root=root, font_file=font)
        if obj.TypeId == "Part::FeaturePython":
            DXFObjectProxy(obj)
        if FreeCAD.GuiUp:
            root = obj.DXFSourceRoot
            source = DXFCAD.drawing_document(root)
            entity = DXFCAD.parse_entity(obj.DXFData, getattr(obj, "DXFContext", ""))
            layer = source.layers.get(entity.layer, source.layers["0"])
            DXFViewProvider(obj.ViewObject, entity.color(layer))
            obj.ViewObject.Visibility = entity.visible
            for group in root.Group:
                if group.Name not in group_names and hasattr(group, "DXFLayerName"):
                    group.ViewObject.Visibility = source.layers.get(group.DXFLayerName, source.layers["0"]).visible
        _check_display(obj)
    return obj


def prepare_move(objects, offset):
    """Return C++ preview records; a zero offset also validates a selection."""
    import DXFCAD
    return DXFCAD.prepare_batch_transform(list(objects), "move", offset=offset,
                                         allow_identity=True).preview_records


def apply_batch_plan(plan, label):
    """Apply domain records and initialize copied objects' presentation."""
    import DXFCAD
    from dxfcad_objects import DXFObjectProxy, DXFViewProvider
    widths = [getattr(obj, "ShowWidths", None) for obj in plan.objects]
    group_names = {g.Name for obj in plan.objects
                   if getattr(obj, "DXFSourceRoot", None) is not None
                   for g in obj.DXFSourceRoot.Group}
    with transaction(plan.document, label):
        result = plan.apply()
        if plan.copies:
            for obj, show_widths in zip(result, widths):
                if obj.TypeId == "Part::FeaturePython":
                    DXFObjectProxy(obj)
                if show_widths is not None and "ShowWidths" in obj.PropertiesList:
                    obj.ShowWidths = show_widths
                if FreeCAD.GuiUp:
                    source = DXFCAD.drawing_document(obj.DXFSourceRoot)
                    entity = DXFCAD.parse_entity(obj.DXFData, getattr(obj, "DXFContext", ""))
                    layer = source.layers.get(entity.layer, source.layers["0"])
                    DXFViewProvider(obj.ViewObject, entity.color(layer))
                    obj.ViewObject.Visibility = entity.visible
                    for group in obj.DXFSourceRoot.Group:
                        if group.Name not in group_names and hasattr(group, "DXFLayerName"):
                            group.ViewObject.Visibility = source.layers.get(group.DXFLayerName, source.layers["0"]).visible
        for obj in result:
            _check_display(obj)
    return result


def move_entities(objects, offset):
    import DXFCAD
    return apply_batch_plan(DXFCAD.prepare_batch_transform(list(objects), "move", offset=offset),
                            "Move DXF objects")


def create_related(prepared, label):
    import DXFCAD
    prepared = list(prepared)
    plan = DXFCAD.prepare_entity_copy([obj for obj, raw in prepared],
                                      [raw for obj, raw in prepared])
    return apply_batch_plan(plan, label)


def copy_entities(objects, offset):
    import DXFCAD
    return apply_batch_plan(DXFCAD.prepare_batch_transform(list(objects), "copy", offset=offset),
                            "Copy DXF objects")


def apply_records(prepared, label):
    import DXFCAD
    prepared = list(prepared)
    plan = DXFCAD.prepare_entity_update([obj for obj, raw in prepared],
                                        [raw for obj, raw in prepared])
    return apply_entity_update(plan, label)


def apply_entity_update(plan, label):
    """Commit a validated C++ replacement plan and refresh its display."""
    with transaction(plan.document, label):
        result = plan.apply()
        for obj in result:
            _check_display(obj)
    return result


def apply_contour_plan(plan, label):
    """Present and commit a validated C++ contour plan in one undo step."""
    import DXFCAD
    from dxfcad_objects import DXFObjectProxy, DXFViewProvider
    show_widths = getattr(plan.template_object, "ShowWidths", None)
    source = DXFCAD.drawing_document(plan.root)
    existing_groups = {g.Name for g in plan.root.Group}
    with transaction(plan.document, label):
        result = plan.apply()
        for obj in plan.created_objects:
            if obj.TypeId == "Part::FeaturePython":
                DXFObjectProxy(obj)
            if show_widths is not None and "ShowWidths" in obj.PropertiesList:
                obj.ShowWidths = show_widths
            if FreeCAD.GuiUp:
                entity = DXFCAD.parse_entity(obj.DXFData)
                layer = source.layers.get(entity.layer, source.layers["0"])
                DXFViewProvider(obj.ViewObject, entity.color(layer))
                obj.ViewObject.Visibility = entity.visible
        if FreeCAD.GuiUp:
            for group in plan.root.Group:
                if group.Name not in existing_groups and hasattr(group, "DXFLayerName"):
                    group.ViewObject.Visibility = source.layers.get(group.DXFLayerName, source.layers["0"]).visible
        for obj in result:
            _check_display(obj)
    return result


def replace_contours(objects, records, label, preserve_first=True):
    """Prepare the C++ replacement plan and commit through the UI adapter."""
    import DXFCAD
    return apply_contour_plan(DXFCAD.prepare_contour_edit(list(objects), list(records), preserve_first), label)


def apply_corner_plan(plan, label):
    """Commit the C++ corner plan and initialize connector presentation."""
    import DXFCAD
    from dxfcad_objects import DXFObjectProxy, DXFViewProvider
    source = DXFCAD.drawing_document(plan.root)
    groups = {g.Name for g in plan.root.Group}
    with transaction(plan.document, label):
        connector = plan.apply()
        if connector.TypeId == "Part::FeaturePython":
            DXFObjectProxy(connector)
        if FreeCAD.GuiUp:
            entity = DXFCAD.parse_entity(connector.DXFData)
            layer = source.layers.get(entity.layer, source.layers["0"])
            DXFViewProvider(connector.ViewObject, entity.color(layer))
            connector.ViewObject.Visibility = entity.visible
            for group in plan.root.Group:
                if group.Name not in groups and hasattr(group, "DXFLayerName"):
                    group.ViewObject.Visibility = source.layers.get(group.DXFLayerName, source.layers["0"]).visible
        for obj in [*plan.objects, connector]:
            _check_display(obj)
    return connector


def apply_line_corner(prepared, connector, connector_type, label):
    """Adapt existing record callers to the validated C++ corner plan."""
    import DXFCAD
    prepared = list(prepared)
    plan = DXFCAD.prepare_corner_edit([obj for obj,raw in prepared],
                                      [raw for obj,raw in prepared], connector, connector_type)
    return apply_corner_plan(plan, label)


def apply_line_fillet(prepared, arc):
    return apply_line_corner(prepared,arc,"ARC","Fillet DXF lines")


def apply_line_chamfer(prepared, connector):
    return apply_line_corner(prepared,connector,"LINE","Chamfer DXF lines")


def prepare_properties(objects, layer_name, color, visible, layer_color=7, layer_on=True, edit_layer=False):
    import DXFCAD
    return DXFCAD.prepare_entity_properties(list(objects), layer_name, color, visible,
                                             layer_color, layer_on, edit_layer)


def apply_properties(objects, layer_name, color, visible, layer_color=7, layer_on=True, edit_layer=False):
    import DXFCAD
    document,roots,sources,records = prepare_properties(objects,layer_name,color,visible,layer_color,layer_on,edit_layer)
    selected = {obj.Name for obj,root,raw in records}
    existing_groups = {g.Name for root in roots.values() for g in root.Group}
    with transaction(document,"Edit DXF properties"):
        document,roots,sources,records = DXFCAD.apply_entity_properties(
            [obj for obj,root,raw in records], layer_name, color, visible,
            layer_color, layer_on, edit_layer)
        for obj,root,raw in records:
            _check_display(obj)
        if FreeCAD.GuiUp:
            from dxfcad_objects import display_color
            for root in roots.values():
                for group in root.Group:
                    if (getattr(group,"DXFLayerName",None)==layer_name
                            and (edit_layer or group.Name not in existing_groups)):
                        group.ViewObject.Visibility = sources[root.Name].layers[layer_name].visible
            for obj in document.Objects:
                root = getattr(obj,"DXFSourceRoot",None)
                if root is None or root.Name not in sources or "DXFData" not in obj.PropertiesList:
                    continue
                entity = DXFCAD.parse_entity(obj.DXFData,getattr(obj,"DXFContext",""))
                source = sources[root.Name]
                layer = source.layers.get(entity.layer,source.layers["0"])
                rgb = display_color(entity.color(layer))
                obj.ViewObject.LineColor = rgb
                obj.ViewObject.ShapeColor = rgb
                obj.ViewObject.PointColor = rgb
                if obj.Name in selected or (edit_layer and entity.layer==layer_name):
                    obj.ViewObject.Visibility = entity.visible and layer.visible
    return [obj for obj,root,raw in records]
