# R8 rules for the minified builds (release and sideload), on top of AGP's
# proguard-android-optimize.txt and the rules each library ships.
#
# The JNI bridge (dev.geode.engine.bridge.*Native) needs no rule here: the default file keeps the
# name of every native method and of its class, and the native code looks up no app class by name.

# Glance creates an ActionCallback (WidgetTransportAction, WidgetStyleAction) from its class name
# with the no-argument constructor. Glance's own rule keeps the class; R8 full mode keeps a
# constructor only when a rule names it.
-keep class * implements androidx.glance.appwidget.action.ActionCallback { public <init>(); }
