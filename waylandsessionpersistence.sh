#!/bin/sh
# SPDX-License-Identifier: MIT
# Sourced by login shells: preserve other generic plugins and avoid duplicates.
case ",${QT_QPA_GENERIC_PLUGINS-}," in
    *,waylandsessionpersistence,*) ;;
    *) QT_QPA_GENERIC_PLUGINS="${QT_QPA_GENERIC_PLUGINS:+${QT_QPA_GENERIC_PLUGINS},}waylandsessionpersistence" ;;
esac
export QT_QPA_GENERIC_PLUGINS
