package dev.geode.export

import androidx.annotation.StringRes

class ExportFailure(
    @StringRes val stringResId: Int,
    cause: Throwable? = null,
) : Exception(cause)
