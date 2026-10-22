"""Shared screen-space snapping for interactive DXFCAD tools."""
import FreeCAD

_INDEX = object()


class _ViewChanges:
    def __init__(self, index):
        self.index = index

    def slotChangedObject(self, view, prop):
        if prop == "Visibility" and self.index is not None:
            self.index.slotChangedObject(view.Object,prop)


class _SnapIndex:
    def __init__(self, document_name):
        import FreeCADGui
        self.document_name = document_name
        self.entries, self.present = [], set()
        self.dirty, self.alive = True, True
        self.view_changes = _ViewChanges(self)
        FreeCAD.addDocumentObserver(self)
        FreeCADGui.addDocumentObserver(self.view_changes)

    def slotChangedObject(self, obj, prop=None):
        if getattr(getattr(obj,"Document",None),"Name",None) == self.document_name:
            self.dirty = True

    slotCreatedObject = slotChangedObject
    slotDeletedObject = slotChangedObject

    def slotDeletedDocument(self, document):
        if document.Name == self.document_name:
            self.close()

    def close(self):
        if self.alive:
            import FreeCADGui
            FreeCAD.removeDocumentObserver(self)
            FreeCADGui.removeDocumentObserver(self.view_changes)
            self.view_changes.index = None
            self.entries = []
            self.alive = False


class _ObjectTargets:
    def __init__(self, key, points, geometry, bounds):
        self.key, self.points, self.geometry, self.bounds = key, points, geometry, bounds
        self.projection = None
        self.screen_points, self.screen_bounds = [], None

    def project(self, view, key):
        if self.projection == key:
            return
        self.screen_points = [view.getPointOnScreen(p) for p,kind in self.points]
        screen = [view.getPointOnScreen(p) for p in self.bounds]
        self.screen_bounds = ((min(p[0] for p in screen), min(p[1] for p in screen),
                               max(p[0] for p in screen), max(p[1] for p in screen)) if screen else None)
        self.projection = key


class ObjectSnapper:
    @staticmethod
    def projection_key(view):
        camera = view.getCameraNode()
        scale = camera.height.getValue() if hasattr(camera,"height") else camera.heightAngle.getValue()
        # Automatic near/far clipping changes during previews do not alter XY
        # projection and must not invalidate every object's screen points.
        return (view.getSize(),tuple(camera.position.getValue().getValue()),
                tuple(camera.orientation.getValue().getValue()),camera.aspectRatio.getValue(),
                camera.viewportMapping.getValue(),scale,hasattr(camera,"height"))

    @staticmethod
    def clear(cache):
        index = cache.get(_INDEX)
        if index is not None:
            index.close()
        cache.clear()

    @staticmethod
    def visible(obj, visited=None, memberships=None, visibility=None):
        visited = set() if visited is None else visited
        memberships = {} if memberships is None else memberships
        visibility = {} if visibility is None else visibility
        if obj.Name in visibility:
            return visibility[obj.Name]
        if obj.Name in visited:
            return True
        visited.add(obj.Name)
        if not obj.ViewObject.isVisible():
            visibility[obj.Name] = False
            return False
        for parent in obj.InList:
            if "Group" in parent.PropertiesList:
                if parent.Name not in memberships:
                    memberships[parent.Name] = {child.Name for child in parent.Group}
                if obj.Name in memberships[parent.Name] and not ObjectSnapper.visible(parent, visited, memberships, visibility):
                    visibility[obj.Name] = False
                    return False
        visibility[obj.Name] = True
        return True

    @staticmethod
    def nearest(document_name, view, position, cache, exclude=None, radius=12):
        import DXFCAD

        nearest, distance, priority = None, radius**2, 99
        memberships, visibility, present = {}, {}, set()
        candidates, active_pairs = [], set()
        projection = ObjectSnapper.projection_key(view)

        def consider(point, source, kind, screen=None):
            nonlocal nearest, distance, priority
            screen = view.getPointOnScreen(point) if screen is None else screen
            squared = (screen[0]-position[0])**2+(screen[1]-position[1])**2
            rank = {"endpoint":0,"vertex":0,"intersection":1,"midpoint":2}.get(kind,3)
            if squared < distance or (nearest is not None and abs(squared-distance)<1e-9 and rank<priority):
                label = (f"{source[0].Label} / {source[1].Label}: {kind}" if isinstance(source,tuple)
                         else f"{source.Label}: {kind}")
                nearest, distance, priority = (point,label,None), squared, rank

        def vector(p):
            return DXFCAD.Vector3D(p.x,p.y,p.z)

        index = cache.get(_INDEX)
        if index is None or not index.alive or index.document_name != document_name:
            if index is not None:
                index.close()
            index = cache[_INDEX] = _SnapIndex(document_name)
        if index.dirty:
            entries = []
            for obj in FreeCAD.getDocument(document_name).Objects:
                if "DXFData" not in obj.PropertiesList:
                    continue
                present.add(obj.Name)
                if not ObjectSnapper.visible(obj, memberships=memberships, visibility=visibility) or getattr(obj,"DisplayStatus","").startswith("Error:"):
                    continue
                placement = obj.Placement
                native = (obj.DXFData, getattr(obj,"DXFContext",""))
                key = (*native,tuple(placement.toMatrix().A))
                cached = cache.get(obj.Name)
                if cached is None or cached.key != key:
                    try:
                        entity = DXFCAD.parse_entity(*native)
                        points = [(placement.multVec(FreeCAD.Vector(p.position.x,p.position.y,p.position.z)),p.kind)
                                  for p in entity.snap_points()]
                        axes = [vector(placement.Rotation.multVec(FreeCAD.Vector(*axis)))
                                for axis in ((1,0,0),(0,1,0),(0,0,1))]
                        geometry = DXFCAD.SnapGeometry(entity,vector(placement.Base),*axes)
                        bounds = [FreeCAD.Vector(p.x,p.y,p.z) for p in geometry.bound_points]
                    except (ValueError, RuntimeError, OverflowError):
                        points, geometry, bounds = [], None, []
                    cached = cache[obj.Name] = _ObjectTargets(key,points,geometry,bounds)
                entries.append((obj,cached))
            index.entries, index.present, index.dirty = entries, present, False
        present = index.present
        for obj,cached in index.entries:
            if obj == exclude:
                continue
            cached.project(view,projection)
            for (point,kind),screen in zip(cached.points,cached.screen_points):
                consider(point,obj,kind,screen)
            if cached.screen_bounds:
                left,bottom,right,top = cached.screen_bounds
                if (left-radius <= position[0] <= right+radius and bottom-radius <= position[1] <= top+radius):
                    candidates.append((obj,cached))
        for i,(first,a) in enumerate(candidates):
            for second,b in candidates[i+1:]:
                pair = ("intersection",first.Name,second.Name)
                active_pairs.add(pair)
                key = (a.key,b.key)
                cached = cache.get(pair)
                if cached is None or cached[0] != key:
                    try:
                        hits = [FreeCAD.Vector(p.position.x,p.position.y,p.position.z)
                                for p in a.geometry.intersections(b.geometry)]
                    except (ValueError, RuntimeError, OverflowError):
                        hits = []
                    cached = cache[pair] = (key,hits)
                for point in cached[1]:
                    consider(point,(first,second),"intersection")
        # Pair results are kept only near the current pointer, bounding cache
        # growth as the user pans through a large drawing.
        for name in list(cache):
            if (isinstance(name,tuple) and name not in active_pairs) or (isinstance(name,str) and name not in present):
                del cache[name]
        return nearest
