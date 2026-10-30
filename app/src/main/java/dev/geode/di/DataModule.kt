package dev.geode.di

import android.content.Context
import android.content.SharedPreferences
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import dev.geode.data.GeodePrefsFiles
import dev.geode.geodeContainer
import dev.geode.ui.UserDataRepository
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import javax.inject.Qualifier
import javax.inject.Singleton

@Qualifier
@Retention(AnnotationRetention.BINARY)
annotation class GeneralPrefs

/** The dispatcher blocking file and prefs I/O runs on; a test passes its own. */
@Qualifier
@Retention(AnnotationRetention.BINARY)
annotation class IoDispatcher

@Module
@InstallIn(SingletonComponent::class)
object DataModule {
    @Provides
    @Singleton
    fun providePrefsFiles(
        @ApplicationContext context: Context,
    ): GeodePrefsFiles = context.geodeContainer.prefsFiles

    @Provides
    @Singleton
    @GeneralPrefs
    fun provideGeneralPrefs(prefsFiles: GeodePrefsFiles): SharedPreferences = prefsFiles.general

    @Provides
    @Singleton
    fun provideUserDataRepository(
        @ApplicationContext context: Context,
    ): UserDataRepository = context.geodeContainer.userData

    @Provides
    @IoDispatcher
    fun provideIoDispatcher(): CoroutineDispatcher = Dispatchers.IO
}
